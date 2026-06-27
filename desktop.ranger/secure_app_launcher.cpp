
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
#include "status_logger.h"
#include "utils.h"
#include "winstation_patcher.h"

namespace SecureApplicationLauncher
{
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

	void LaunchAppAndWaitNewWindow(IN std::wstring DesktopName,
								   IN std::wstring CommandLine)
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

		// CommandLine должен быть writable
		std::vector<wchar_t> cmdBuffer(exeCommandLine.begin(), exeCommandLine.end());
		cmdBuffer.push_back(0);

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
		auto unhookOnExit = wil::scope_exit([&] {
			if (!::UnhookWinEvent(newWindowHook)) {
				LOG_ERR(std::format(L"Failed to unhook event: {}", GetLastError()));
			}
		});

		PROCESS_INFORMATION processInformation{};
		STARTUPINFOW startupInformation{};
		startupInformation.cb = sizeof(startupInformation);
		startupInformation.lpDesktop = const_cast<wchar_t *>(DesktopName.data());
		startupInformation.dwFlags = STARTF_USESHOWWINDOW;
		startupInformation.wShowWindow = SW_SHOW;

		LOG_INFO(std::format(L"CreateProcessA \"{}\"", cmdBuffer.data()));
		const auto status =
			::CreateProcessW(nullptr, cmdBuffer.data(), nullptr, nullptr, 0, 0, nullptr,
							 nullptr, &startupInformation, &processInformation);

		if (status && processInformation.hProcess && processInformation.hThread) {

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

		} else {
			LOG_ERR(std::format(L"CreateProcessA  failed \"{}\"", CommandLine));
		}
	}

	std::wstring GetUserSidString(HANDLE processHandle)
	{
		if (!processHandle) {
			LOG_ERR(L"Invalid process handle");
			return {};
		}

		wil::unique_handle token;
		if (!::OpenProcessToken(processHandle, TOKEN_QUERY, token.addressof())) {
			LOG_ERR(L"OpenProcessToken failed");
			return {};
		}

		DWORD size{};
		::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			LOG_ERR(L"GetTokenInformation failed");
			return {};
		}

		std::vector<BYTE> buffer(size);
		if (!::GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) {
			LOG_ERR(L"GetTokenInformation failed");
			return {};
		}

		wchar_t *sid{};
		if (!::ConvertSidToStringSidW(
				reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid)) {
			LOG_ERR(L"ConvertSidToStringSidA failed");
			return {};
		}

		std::unique_ptr<wchar_t, decltype(&::LocalFree)> freeOnExit(sid, ::LocalFree);
		return sid;
	}

	void LaunchAppOnSecureDesktop(IN HDESK SecretDesktopHandle,
								  IN std::wstring DesktopName,
								  IN std::wstring CommandLine)
	{
		if (!SecretDesktopHandle || DesktopName.empty() || CommandLine.empty()) {
			LOG_ERR(L"Invalid input");
			return;
		}

		// --- Получаем SID пользователя ---
		const auto userSidString = GetUserSidString(::GetCurrentProcess());
		if (userSidString.empty()) {
			return;
		}

		SecurityDescriptor sdAllow(std::format(L"D:(A;;GA;;;{})", userSidString));
		if (!sdAllow.Initialized()) {
			return;
		}

		SecurityDescriptor sdRestrict(L"D:P");
		if (!sdRestrict.Initialized()) {
			return;
		}

		if (SecretDesktopHandle == ::GetThreadDesktop(::GetCurrentThreadId()) &&
			DesktopName == std::wstring(L"Default")) {
			LaunchAppAndWaitNewWindow(DesktopName, CommandLine);
		} else {

			// Step 1 - restrict winsta access
			WinstationPatcher::RevokeWinsta();

			// Step 2 - set allow permission
			if (!sdAllow.SetDescriptor(SecretDesktopHandle))
				return;

			// Step 3 - run and and wait
			LaunchAppAndWaitNewWindow(DesktopName, CommandLine);

			// Step 4 - restore Restricted permissions
			if (!sdRestrict.SetDescriptor(SecretDesktopHandle)) {
				return;
			}

			// Step 5 - grant winsta access
			WinstationPatcher::GrantWinsta();
		}
	}

} // namespace SecureApplicationLauncher