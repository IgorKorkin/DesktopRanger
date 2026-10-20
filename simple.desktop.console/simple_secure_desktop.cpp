#include <cstddef>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Aclapi.h>
#include <Sddl.h>
#include <tchar.h>
#include <wil/resource.h>

#include "../desktop.ranger.core/desktop_lifecycle.h"
#include "../desktop.ranger.gui/paint_screen_border.h"
#include "internal/simple_desktop_internal.h"
#include "simple_secure_desktop.h"

namespace
{
	/// Память из `ConvertSidToStringSidW` освобождается только через `LocalFree`.
	/// В C++26 для такого паттерна предусмотрен `std::unique_resource`; пока в MSVC STL
	/// его нет — тот же смысл даёт `unique_ptr` с deleter.
	struct LocalFreeDeleter {
		void operator()(wchar_t *p) const noexcept
		{
			if (p) {
				::LocalFree(p);
			}
		}
	};
	using SidLocalString = std::unique_ptr<wchar_t, LocalFreeDeleter>;
} // namespace

namespace BasicDesktop
{
	bool SwitchToDesktop(IN HDESK TargetDesktopHandle)
	{
		bool result{};
		if (::SetThreadDesktop(TargetDesktopHandle)) {
			const auto uThreadId = GetCurrentThreadId();
			if (::GetThreadDesktop(uThreadId) == TargetDesktopHandle) {
				if (::SwitchDesktop(TargetDesktopHandle)) {
					result = true;
				} else {
					::MessageBoxW(nullptr, L"Can't switch desktop", L"",
								  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
				}
			} else {
				::MessageBoxW(nullptr, L"We are on a wrong desktop", L"",
							  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
			}
		} else {
			auto err = ::GetLastError();
			::MessageBoxW(nullptr, L"Can't set desktop", L"",
						  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
		}

		return result;
	}
} // namespace BasicDesktop

namespace SimpleSecureDesktop
{
	std::expected<void, DWORD> detail::LaunchAppOnDesktop(const wchar_t *DesktopName,
														  wchar_t *CommandLine,
														  const SimpleApi &api)
	{
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.lpDesktop = const_cast<wchar_t *>(DesktopName);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOW;
		PROCESS_INFORMATION pi{};

		if (!api.createProcess(nullptr, CommandLine, nullptr, nullptr, 0, 0, nullptr,
							   nullptr, &si, &pi)) {
			const auto error = ::GetLastError();
			return std::unexpected(error ? error : ERROR_GEN_FAILURE);
		}
		auto closeProcess = wil::scope_exit([&]() noexcept {
			if (pi.hThread) {
				api.closeHandle(pi.hThread);
			}
			if (pi.hProcess) {
				api.closeHandle(pi.hProcess);
			}
		});
		api.waitForInputIdle(pi.hProcess, 200);
		return {};
	}

	std::expected<void, DWORD> LaunchAppOnDesktop(const wchar_t *DesktopName,
												  wchar_t *CommandLine)
	{
		return detail::LaunchAppOnDesktop(DesktopName, CommandLine, {});
	}

	bool CreateNewDesktop(IN const wchar_t *DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle)
	{
		bool result = false;
		SecretDesktopHandle = nullptr;

		DefaultDesktopHandle = ::GetThreadDesktop(::GetCurrentThreadId());

		if (DefaultDesktopHandle == nullptr) {
			// DWORD Error = GetLastError();
			// TODO: print errr
			return false;
		}

		// Получаем SID текущего пользователя
		::HANDLE hToken = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			::MessageBoxA(nullptr, "Failed to open process token", "Error", MB_OK);
			return false;
		}

		DWORD dwSize = 0;
		::GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);

		std::vector<std::byte> tokenUserBuf(static_cast<std::size_t>(dwSize));
		auto *const pTokenUser = reinterpret_cast<TOKEN_USER *>(tokenUserBuf.data());

		if (!::GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
			::CloseHandle(hToken);
			::MessageBoxA(nullptr, "Failed to get token information", "Error", MB_OK);
			return false;
		}

		// Конвертируем SID в строку (буфер выделяет API → LocalFree в деструкторе
		// SidLocalString)
		wchar_t *sidRaw = nullptr;
		if (!::ConvertSidToStringSidW(pTokenUser->User.Sid, &sidRaw)) {
			::CloseHandle(hToken);
			::MessageBoxA(nullptr, "Failed to convert SID to string", "Error", MB_OK);
			return false;
		}
		SidLocalString sidString(sidRaw);
		::CloseHandle(hToken);

		//////////////////////////////////////////////////////////////////////////

		constexpr ACCESS_MASK deskFlags = DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU |
										  DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS |
										  DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE;

		// ЗАПРЕЩЕННЫЕ ФЛАГИ (не давать ни в коем случае):
		// DESKTOP_HOOKCONTROL     0x00002000 - разрешает хуки
		// DESKTOP_JOURNALRECORD   0x00004000 - запись действий
		// DESKTOP_JOURNALPLAYBACK 0x00008000 - воспроизведение
		// WRITE_DAC (0x40000)	Запрещает изменение прав
		// WRITE_OWNER (0x80000)	Запрещает смену владельца

		const std::wstring sddl = std::format(
			L"D:(A;;0x{:08X};;;{})", // текущий пользователь
			static_cast<unsigned>(deskFlags),
			sidString ? std::wstring_view{ sidString.get() } : std::wstring_view{});

		// Конвертируем SDDL в SECURITY_DESCRIPTOR
		PSECURITY_DESCRIPTOR securityDescriptor{};
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl.c_str(), SDDL_REVISION_1, &securityDescriptor, nullptr)) {
			::MessageBoxA(nullptr, "Failed to convert security descriptor", "Error",
						  MB_OK);
			return false;
		}

		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = FALSE;
		securityAttributes.lpSecurityDescriptor = securityDescriptor;

		SecretDesktopHandle = ::CreateDesktopW(DesktopName, nullptr, nullptr, 0,
											   deskFlags, &securityAttributes);

		::LocalFree(securityDescriptor);

		return SecretDesktopHandle != nullptr;
	}

	namespace
	{
		void ReportRestoreFailure(
			const DesktopRanger::DesktopLifecycle::RestoreError &first,
			const std::expected<void, DesktopRanger::DesktopLifecycle::RestoreError>
				&retry,
			RunError &errors, const detail::SimpleApi &api) noexcept
		{
			std::fprintf(stderr,
						 "Simple Desktop: restore failed (visible=%lu, thread=%lu); "
						 "retry %s (visible=%lu, thread=%lu).\n",
						 first.switchError.value_or(0), first.threadError.value_or(0),
						 retry ? "succeeded" : "failed",
						 retry ? 0UL : retry.error().switchError.value_or(0),
						 retry ? 0UL : retry.error().threadError.value_or(0));
			std::fflush(stderr);
			::OutputDebugStringW(
				L"Simple Desktop: Desktop restoration failed; see stderr.\n");
			if (retry) {
				return;
			}

			// The original Desktop may still be invisible. Notify the active Desktop
			// while target is still owned, rather than blocking on a hidden main-thread
			// UI.
			if (!api.messageBox(
					nullptr,
					L"Desktop restoration failed after a retry. The original Desktop "
					L"may still be hidden. See the console log. Use the Windows security "
					L"screen (Ctrl+Alt+Delete) to recover the session.",
					L"Simple Desktop restoration error",
					MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SERVICE_NOTIFICATION)) {
				const auto error = ::GetLastError();
				errors.notificationError = error ? error : ERROR_GEN_FAILURE;
				std::fprintf(stderr, "Simple Desktop: error notification failed (%lu).\n",
							 *errors.notificationError);
				std::fflush(stderr);
				::OutputDebugStringW(L"Simple Desktop: error notification failed.\n");
			}
		}
	} // namespace

	void detail::RunDesktopWorker(HDESK target, HDESK visibleOriginal, RunError &errors,
								  const SimpleApi &api) noexcept
	{
		const auto original = api.getCurrentDesktop();
		if (!original) {
			errors.operationError = original.error();
			return;
		}
		HDESK workerOriginal = original.value();
		auto restoreDesktop = wil::scope_exit([&]() noexcept {
			const auto restored = api.restoreDesktop(visibleOriginal, workerOriginal);
			if (!restored) {
				// Keep the first restore errors separate from the operation error.
				errors.visibleRestoreError = restored.error().switchError;
				errors.threadRestoreError = restored.error().threadError;
				const auto retry = api.restoreDesktop(visibleOriginal, workerOriginal);
				ReportRestoreFailure(restored.error(), retry, errors, api);
			}
		});

		const auto switched = api.switchToDesktop(target);
		if (!switched) {
			errors.operationError = switched.error().error;
			errors.rollbackError = switched.error().rollbackError;
			return;
		}
		workerOriginal = switched.value();

		const auto borders = api.createBorders(RGB(255, 255, 0));
		if (!borders) {
			errors.operationError = borders.error();
			return;
		}
		auto destroyBorders = wil::scope_exit([&]() noexcept {
			const auto destroyed = api.destroyBorders(*borders);
			if (!destroyed) {
				errors.windowCleanupError = destroyed.error();
			}
		});
		if (!api.messageBox(
				nullptr, L"You are on the Simple Desktop. Click OK to switch back.",
				L"SimpleSecretDesktop",
				MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SERVICE_NOTIFICATION)) {
			const auto error = ::GetLastError();
			errors.operationError = error ? error : ERROR_GEN_FAILURE;
		}
	}

	std::expected<void, RunError> RunSimpleDesktop()
	try {
		static constexpr wchar_t desktopName[] = L"SimpleSecretDesktop";
		HDESK visibleOriginal{};
		HDESK rawDesktop{};
		if (!CreateNewDesktop(desktopName, visibleOriginal, rawDesktop)) {
			const auto error = ::GetLastError();
			return std::unexpected(
				RunError{ .operationError = error ? error : ERROR_GEN_FAILURE });
		}
		DesktopRanger::DesktopLifecycle::UniqueDesktop desktop{ rawDesktop };

		wchar_t commandLine[] = L"explorer shell:AppsFolder";
		const auto launched = LaunchAppOnDesktop(desktopName, commandLine);
		if (!launched) {
			return std::unexpected(RunError{ .operationError = launched.error() });
		}

		RunError errors{};
		{
			std::jthread worker([&]() noexcept {
				detail::RunDesktopWorker(desktop.get(), visibleOriginal, errors, {});
			});
			worker.join();
		}

		if (errors.operationError || errors.rollbackError || errors.windowCleanupError ||
			errors.visibleRestoreError || errors.threadRestoreError ||
			errors.notificationError) {
			return std::unexpected(errors);
		}
		return {};
	} catch (const std::bad_alloc &) {
		return std::unexpected(RunError{ .operationError = ERROR_NOT_ENOUGH_MEMORY });
	} catch (...) {
		return std::unexpected(RunError{ .operationError = ERROR_GEN_FAILURE });
	}

} // namespace SimpleSecureDesktop
