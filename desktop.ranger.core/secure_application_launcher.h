#pragma once

#include <Windows.h>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>

namespace DesktopRanger::SecureApplicationLauncher
{
	enum class LaunchStep {
		Validate,
		Busy,
		UserSid,
		OpenStation,
		RestrictStation,
		SnapshotDesktop,
		AllowDesktop,
		CreateProcess,
		Callback
	};
	struct LaunchFailure {
		LaunchStep step;
		DWORD error;
	};
	struct LaunchError {
		std::optional<LaunchFailure> failure;
		std::optional<DWORD> desktopRestoreError;
		std::optional<DWORD> stationRestoreError;
		DWORD processId{};
	};

	// Creates a process without changing ACLs; the callback only borrows its handles.
	[[nodiscard]] std::expected<DWORD, LaunchError> CreateApplication(
		std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted);
	// Install the application's window hook before calling. onStarted borrows both
	// process handles until it returns; both are closed before this function returns.
	// Temporary permissions remain in effect throughout onStarted, except when
	// desktopName is "Default" and desktop is the calling thread's Desktop:
	// then neither Window Station nor Desktop ACLs are changed. Desktop is borrowed.
	// Errors after CreateProcess retain its PID; no process is terminated on failure.
	[[nodiscard]] std::expected<DWORD, LaunchError> LaunchApplication(
		HDESK desktop, std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted);
} // namespace DesktopRanger::SecureApplicationLauncher
