
#include <atomic>
#include <expected>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Aclapi.h>
#include <Sddl.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

#include <wil/com.h>
#include <wil/resource.h>

#include "secure_app_launcher.h"
#include "secure_application_launcher.h"
#include "status_logger.h"

namespace SecureApplicationLauncher
{
	// Cleanup must continue even when formatting or logging fails during unwinding.
	void ReportCleanupFailure(const wchar_t *operation, DWORD error) noexcept
	{
		try {
			LOG_ERR(std::format(L"{} failed: {}", operation, error));
		} catch (...) {
			::OutputDebugStringW(L"DesktopRanger: ACL restoration failed.\n");
		}
	}
	struct WindowInfo {
		HWND hwnd{};
		std::wstring title{};
		DWORD processId;
		std::wstring processName{};
	};

	std::atomic<bool> g_NewWindowFound{ false };
	std::mutex g_WindowInfoMutex{};
	WindowInfo g_WindowInfo{};

	// Проверка, является ли окно "главным" пользовательским
	bool IsUserMainWindow(HWND hwnd)
	{
		if (!::IsWindowVisible(hwnd))
			return false;

		if (::GetParent(hwnd) != nullptr)
			return false; // должно быть верхнего уровня

		// 		LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
		// 		if (!(exStyle & WS_EX_APPWINDOW))
		// 			return false; // только окна приложений

		wchar_t title[256]{};
		if (::GetWindowText(hwnd, title, _countof(title)) == 0)
			return false; // без заголовка пропускаем

		wchar_t className[256]{};
		if (!::GetClassNameW(hwnd, className, _countof(className))) {
			return true;
		}

		std::wstring classView(className);
		if (classView == L"CabinetWClass") {
			return true;
		}

		std::wstring wTitle(title);
		if (wTitle.find(L"HwndWrapper") != std::wstring::npos)
			return false; // игнорируем служебные WPF окна

		return true;
	}

	std::wstring GetProcessNameByPid(DWORD processId)
	{
		// Открываем процесс через WIL
		wil::unique_handle hProcess(
			::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
		if (!hProcess) {
			return L"<unknown>";
		}

		wchar_t processName[MAX_PATH]{};
		DWORD size = MAX_PATH;
		if (::QueryFullProcessImageNameW(hProcess.get(), 0, processName, &size)) {
			return std::filesystem::path(processName).filename().wstring();
		}

		return L"<unknown>";
	}

	void CALLBACK NewWindowDispatcher(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
									  LONG idObject, LONG idChild, DWORD dwEventThread,
									  DWORD dwmsEventTime)
	{
		if (idObject != OBJID_WINDOW)
			return;

		if (!IsUserMainWindow(hwnd))
			return;

		{
			wchar_t title[256]{};
			::GetWindowText(hwnd, title, _countof(title));

			std::lock_guard<std::mutex> lock(g_WindowInfoMutex);
			g_WindowInfo.hwnd = hwnd;
			g_WindowInfo.title = title;
			DWORD processId = 0;
			::GetWindowThreadProcessId(hwnd, &processId);

			g_WindowInfo.processName = GetProcessNameByPid(processId);
			g_WindowInfo.processId = processId;

			std::uintptr_t hwndValue = reinterpret_cast<std::uintptr_t>(hwnd);

			std::printf("New top window: HWND=0x%p Title='%ws' App:'%ws' \n",
						g_WindowInfo.hwnd, g_WindowInfo.title.c_str(),
						g_WindowInfo.processName.c_str());
		}

		g_NewWindowFound = true;
	}

	std::wstring ResolveShortcut(std::wstring lnkPath)
	{

		CoInitialize(nullptr);

		wil::com_ptr<IShellLinkW> psl;

		HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
									  IID_PPV_ARGS(&psl));
		if (FAILED(hr) || !psl)
			return {};

		wil::com_ptr<IPersistFile> ppf;
		hr = psl->QueryInterface(IID_PPV_ARGS(&ppf));
		if (FAILED(hr) || !ppf) {
			return {};
		}

		std::wstring fullPath = std::filesystem::absolute(lnkPath).wstring();

		// Конвертируем в LPCOLESTR (UTF-16)
		hr = ppf->Load(fullPath.c_str(), STGM_READ);
		if (FAILED(hr)) {
			return {};
		}

		wchar_t exePath[MAX_PATH]{};
		hr = psl->GetPath(exePath, MAX_PATH, nullptr, SLGP_UNCPRIORITY);
		if (FAILED(hr)) {
			return {};
		}

		// TODO: psl->GetArguments()

		CoUninitialize();

		return exePath;
	}

	static void LaunchAndWait(HDESK desktop, bool applyPolicy,
							  IN std::wstring DesktopName, IN std::wstring CommandLine)
	{
		LOG_INFO(std::format(L"LaunchAppAndWaitNewWindow \"{}\"", CommandLine));

		std::filesystem::path filePath(CommandLine);
		auto ext = filePath.extension().wstring();

		std::ranges::transform(ext, ext.begin(),
							   [](wchar_t c) { return std::toupper(c); });

		std::wstring exeCommandLine;
		if (ext == L".EXE") {
			exeCommandLine = CommandLine;
		} else if (ext == L".LNK") {

			exeCommandLine = ResolveShortcut(CommandLine);
			LOG_INFO(std::format(L"ResolveShortcut \"{}\"", exeCommandLine));

			if (exeCommandLine.empty()) {
				LOG_INFO(std::format(L"Fail to resolve shortcut: \"{}\" -> \"{}\"",
									 CommandLine, exeCommandLine));
				return;
			}
		} else {
			LOG_INFO(std::format(L"Unsupported file extension: \"{}\"", ext));
			return;
		}

		g_NewWindowFound = false;

		// Ставим хук на все новые окна на рабочем столе
		const auto newWindowHook =
			::SetWinEventHook(EVENT_OBJECT_SHOW, // событие начала видимости окна
							  EVENT_OBJECT_SHOW, nullptr, NewWindowDispatcher,
							  0,					// все процессы
							  0,					// все потоки
							  WINEVENT_OUTOFCONTEXT // не в контексте процесса, глобально
			);
		if (!newWindowHook) {
			LOG_ERR(std::format(L"Failed to set hook  failed"));
			return;
		}

		// RAII для автоматического снятия хука
		auto unhookOnExit = wil::scope_exit([&]() noexcept {
			if (!::UnhookWinEvent(newWindowHook)) {
				ReportCleanupFailure(L"UnhookWinEvent", ::GetLastError());
			}
		});

		namespace Launcher = DesktopRanger::SecureApplicationLauncher;
		auto waitForWindow = [&](const PROCESS_INFORMATION &processInformation)
			-> std::expected<void, DWORD> {
			DWORD waitMaxDurationMs = 5000; // 7 секунд

			const auto result =
				::WaitForInputIdle(processInformation.hProcess, waitMaxDurationMs);

			MSG msg{};
			const auto startTime = ::GetTickCount64();
			while (!g_NewWindowFound) {

				while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
					::TranslateMessage(&msg);
					::DispatchMessage(&msg);
				}
				Sleep(10);

				// Проверяем таймаут
				if (GetTickCount64() - startTime > waitMaxDurationMs) {
					LOG_INFO(L"Timeout waiting for new window.");
					break;
				}
			}

			if (g_NewWindowFound) {
				// Достаем информацию о новом окне
				WindowInfo newWin{};
				{
					std::lock_guard<std::mutex> lock(g_WindowInfoMutex);
					g_NewWindowFound = false;
					newWin = g_WindowInfo;
				}
				LOG_INFO(std::format(L"WaitForInputIdle {}", result));
				LOG_INFO(std::format(L"A new window appeared \"{}\":\"{}\":{} : {:p}",
									 std::wstring(newWin.title),
									 std::wstring(newWin.processName), newWin.processId,
									 static_cast<void *>(newWin.hwnd)));
			} else {
				LOG_INFO(L"No new window appeared within timeout for process");
			}

			return {};
		};
		const auto result =
			applyPolicy
				? Launcher::LaunchApplication(desktop, DesktopName, exeCommandLine,
											  waitForWindow)
				: Launcher::CreateApplication(DesktopName, exeCommandLine, waitForWindow);
		if (!result) {
			const auto &error = result.error();
			LOG_ERR(std::format(L"Application launch failed; PID: {}", error.processId));
			if (error.failure) {
				ReportCleanupFailure(L"Application launch/callback",
									 error.failure->error);
			}
			if (error.desktopRestoreError) {
				ReportCleanupFailure(L"Restore Desktop DACL", *error.desktopRestoreError);
			}
			if (error.stationRestoreError) {
				ReportCleanupFailure(L"Restore Window Station DACL",
									 *error.stationRestoreError);
			}
		}
	}

	// Both public entry points share hook state and may pump reentrant messages.
	static std::atomic_flag launchInProgress = ATOMIC_FLAG_INIT;

	void LaunchAppAndWaitNewWindow(IN std::wstring DesktopName,
								   IN std::wstring CommandLine)
	{
		if (launchInProgress.test_and_set()) {
			LOG_ERR(L"Another application launch is already in progress");
			return;
		}
		auto finish = wil::scope_exit([&]() noexcept { launchInProgress.clear(); });
		LaunchAndWait(nullptr, false, std::move(DesktopName), std::move(CommandLine));
	}

	void LaunchAppOnSecureDesktop(IN HDESK SecretDesktopHandle,
								  IN std::wstring DesktopName,
								  IN std::wstring CommandLine)
	{
		if (!SecretDesktopHandle || DesktopName.empty() || CommandLine.empty()) {
			LOG_ERR(L"Invalid input");
			return;
		}
		if (launchInProgress.test_and_set()) {
			LOG_ERR(L"Another secure application launch is already in progress");
			return;
		}
		auto finish = wil::scope_exit([&]() noexcept { launchInProgress.clear(); });
		LaunchAndWait(SecretDesktopHandle, true, std::move(DesktopName),
					  std::move(CommandLine));
	}
} // namespace SecureApplicationLauncher
