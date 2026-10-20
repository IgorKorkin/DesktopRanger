#include <cstddef>
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
	void LaunchAppOnDesktop(IN const wchar_t *DesktopName, IN wchar_t *CommandLine)
	{
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.lpDesktop = const_cast<wchar_t *>(DesktopName);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOW;
		PROCESS_INFORMATION pi{};

		const auto launched = ::CreateProcessW(nullptr, CommandLine, nullptr, nullptr, 0,
											   0, nullptr, nullptr, &si, &pi);
		if (launched) {
			::WaitForInputIdle(pi.hProcess, 200);
			::CloseHandle(pi.hThread);
			::CloseHandle(pi.hProcess);
		}
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
		LaunchAppOnDesktop(desktopName, commandLine);

		RunError errors{};
		HDESK workerOriginal{};
		{
			std::jthread worker([&]() noexcept {
				const auto original =
					DesktopRanger::DesktopLifecycle::GetCurrentDesktop();
				if (!original) {
					errors.operationError = original.error();
					return;
				}
				workerOriginal = original.value();
				auto restoreDesktop = wil::scope_exit([&]() noexcept {
					const auto restored = DesktopRanger::DesktopLifecycle::RestoreDesktop(
						visibleOriginal, workerOriginal);
					if (!restored) {
						errors.visibleRestoreError = restored.error().switchError;
						errors.threadRestoreError = restored.error().threadError;
					}
				});

				const auto switched =
					DesktopRanger::DesktopLifecycle::SwitchToDesktop(desktop.get());
				if (!switched) {
					errors.operationError = switched.error().error;
					errors.rollbackError = switched.error().rollbackError;
					return;
				}
				workerOriginal = switched.value();

				const auto borders =
					PaintScreenBorder::CreateScreenBorderWindows(RGB(255, 255, 0));
				if (!borders) {
					errors.operationError = borders.error();
					return;
				}
				auto destroyBorders = wil::scope_exit([&]() noexcept {
					const auto destroyed =
						PaintScreenBorder::DestroyScreenBorderWindows(*borders);
					if (!destroyed) {
						errors.windowCleanupError = destroyed.error();
					}
				});
				::MessageBoxW(
					nullptr, L"You are on the Simple Desktop. Click OK to switch back.",
					desktopName,
					MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SERVICE_NOTIFICATION);
			});
			worker.join();
		}

		if (errors.operationError || errors.rollbackError || errors.windowCleanupError ||
			errors.visibleRestoreError || errors.threadRestoreError) {
			return std::unexpected(errors);
		}
		return {};
	} catch (const std::bad_alloc &) {
		return std::unexpected(RunError{ .operationError = ERROR_NOT_ENOUGH_MEMORY });
	} catch (...) {
		return std::unexpected(RunError{ .operationError = ERROR_GEN_FAILURE });
	}

} // namespace SimpleSecureDesktop
