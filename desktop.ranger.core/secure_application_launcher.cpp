#include "internal/secure_application_launcher_internal.h"
#include "security_descriptor.h"

#include <Sddl.h>
#include <atomic>
#include <format>
#include <new>
#include <vector>
#include <wil/resource.h>

namespace DesktopRanger::SecureApplicationLauncher
{
	namespace Policy = WindowStationPolicy;

	std::expected<std::wstring, DWORD> detail::GetUserSidString(const LaunchApi &api)
	{
		HANDLE token{};
		if (!api.openProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return std::unexpected(::GetLastError());
		}
		auto closeToken = wil::scope_exit([&]() noexcept { api.closeHandle(token); });
		DWORD size{};
		const auto probe = api.getTokenInformation(token, TokenUser, nullptr, 0, &size);
		const auto error = ::GetLastError();
		if (probe || error != ERROR_INSUFFICIENT_BUFFER || size < sizeof(TOKEN_USER)) {
			return std::unexpected(
				probe || error == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : error);
		}
		std::vector<BYTE> buffer(size);
		if (!api.getTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
			return std::unexpected(::GetLastError());
		}
		wchar_t *sid{};
		if (!::ConvertSidToStringSidW(
				reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid)) {
			return std::unexpected(::GetLastError());
		}
		wil::unique_hlocal_string owner{ sid };
		return std::wstring(sid);
	}

	std::expected<DWORD, LaunchError> detail::CreateApplication(
		std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted,
		const LaunchApi &api)
	{
		LaunchError error{};
		auto fail = [&](LaunchStep step, DWORD code) {
			error.failure = LaunchFailure{ step, code };
		};
		if (desktopName.empty() || commandLine.empty() || !onStarted ||
			desktopName.find(L'\0') != std::wstring_view::npos ||
			commandLine.find(L'\0') != std::wstring_view::npos) {
			fail(LaunchStep::Validate, ERROR_INVALID_PARAMETER);
			return std::unexpected(error);
		}
		auto operation = [&] {
			std::wstring name(desktopName);
			std::wstring command(commandLine);
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.lpDesktop = name.data();
			startup.dwFlags = STARTF_USESHOWWINDOW;
			startup.wShowWindow = SW_SHOW;
			PROCESS_INFORMATION process{};
			auto closeProcess = wil::scope_exit([&]() noexcept {
				if (process.hThread)
					api.closeHandle(process.hThread);
				if (process.hProcess)
					api.closeHandle(process.hProcess);
			});
			if (!api.createProcess(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
								   nullptr, nullptr, &startup, &process)) {
				fail(LaunchStep::CreateProcess, ::GetLastError());
				return;
			}
			error.processId = process.dwProcessId;
			if (!process.hThread || !process.hProcess) {
				fail(LaunchStep::CreateProcess, ERROR_INVALID_HANDLE);
				return;
			}
			try {
				if (auto result = onStarted(process); !result) {
					fail(LaunchStep::Callback, result.error());
				}
			} catch (const std::bad_alloc &) {
				fail(LaunchStep::Callback, ERROR_NOT_ENOUGH_MEMORY);
			} catch (...) {
				fail(LaunchStep::Callback, ERROR_UNHANDLED_EXCEPTION);
			}
		};
		try {
			operation();
		} catch (const std::bad_alloc &) {
			fail(LaunchStep::CreateProcess, ERROR_NOT_ENOUGH_MEMORY);
		} catch (...) {
			fail(LaunchStep::CreateProcess, ERROR_UNHANDLED_EXCEPTION);
		}
		if (error.failure)
			return std::unexpected(error);
		return error.processId;
	}

	std::expected<DWORD, LaunchError> CreateApplication(
		std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted)
	{
		return detail::CreateApplication(desktopName, commandLine, onStarted, {});
	}

	std::expected<DWORD, LaunchError> detail::LaunchApplication(
		HDESK desktop, std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted,
		const LaunchApi &api)
	{
		LaunchError error{};
		auto fail = [&](LaunchStep step, DWORD code) {
			error.failure = LaunchFailure{ step, code };
		};
		if (!desktop || desktopName.empty() || commandLine.empty() || !onStarted ||
			desktopName.find(L'\0') != std::wstring_view::npos ||
			commandLine.find(L'\0') != std::wstring_view::npos) {
			fail(LaunchStep::Validate, ERROR_INVALID_PARAMETER);
			return std::unexpected(error);
		}
		static std::atomic_flag inProgress = ATOMIC_FLAG_INIT;
		if (inProgress.test_and_set()) {
			fail(LaunchStep::Busy, ERROR_BUSY);
			return std::unexpected(error);
		}
		auto finishLaunch = wil::scope_exit([&]() noexcept { inProgress.clear(); });

		// Evaluate the result only after all cleanup guards have updated error.
		auto operation = [&] {
			auto sid = GetUserSidString(api);
			if (!sid) {
				fail(LaunchStep::UserSid, sid.error());
				return;
			}
			SecurityDescriptor allow(std::format(L"D:(A;;GA;;;{})", sid.value()));
			if (!allow.Initialized()) {
				fail(LaunchStep::AllowDesktop, ::GetLastError());
				return;
			}
			auto createAndWait = [&] {
				auto result =
					detail::CreateApplication(desktopName, commandLine, onStarted, api);
				if (result)
					error.processId = result.value();
				else
					error = result.error();
			};
			if (desktopName == L"Default" &&
				desktop == api.getThreadDesktop(::GetCurrentThreadId())) {
				createAndWait();
				return;
			}

			HWINSTA station =
				api.openWindowStation(L"WinSta0", FALSE, READ_CONTROL | WRITE_DAC);
			if (!station) {
				fail(LaunchStep::OpenStation, ::GetLastError());
				return;
			}
			auto closeStation =
				wil::scope_exit([&]() noexcept { api.closeWindowStation(station); });
			Policy::RestrictedSession session(station, nullptr, api.dacl);
			if (auto result = session.Restrict(); !result) {
				fail(LaunchStep::RestrictStation, result.error());
				return;
			}
			auto restoreStation = wil::scope_exit([&]() noexcept {
				if (auto result = session.Restore(); !result) {
					error.stationRestoreError = result.error();
					// Retain the snapshot on failure and retry before closing the
					// station.
					(void)session.Restore();
				}
			});
			auto snapshot = Policy::detail::SnapshotDacl(desktop, api.dacl);
			if (!snapshot) {
				fail(LaunchStep::SnapshotDesktop, snapshot.error());
				return;
			}
			auto restoreDesktop = wil::scope_exit([&]() noexcept {
				if (auto result =
						Policy::detail::RestoreDacl(desktop, snapshot->get(), api.dacl);
					!result) {
					error.desktopRestoreError = result.error();
					(void)Policy::detail::RestoreDacl(desktop, snapshot->get(), api.dacl);
				}
			});
			auto dacl = Policy::GetDacl(allow.GetDescriptor());
			if (!dacl) {
				fail(LaunchStep::AllowDesktop, dacl.error());
				return;
			}
			if (auto result = Policy::detail::ApplyDacl(desktop, dacl.value(), api.dacl);
				!result) {
				fail(LaunchStep::AllowDesktop, result.error());
				return;
			}
			createAndWait();
		};
		try {
			operation();
		} catch (const std::bad_alloc &) {
			fail(LaunchStep::Validate, ERROR_NOT_ENOUGH_MEMORY);
		} catch (...) {
			fail(LaunchStep::Validate, ERROR_UNHANDLED_EXCEPTION);
		}
		if (error.failure || error.desktopRestoreError || error.stationRestoreError) {
			return std::unexpected(error);
		}
		return error.processId;
	}

	std::expected<DWORD, LaunchError> LaunchApplication(
		HDESK desktop, std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted)
	{
		return detail::LaunchApplication(desktop, desktopName, commandLine, onStarted,
										 {});
	}
} // namespace DesktopRanger::SecureApplicationLauncher
