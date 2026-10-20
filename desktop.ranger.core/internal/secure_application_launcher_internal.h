#pragma once

#include "../secure_application_launcher.h"
#include "window_station_policy_internal.h"
#include <string>

namespace DesktopRanger::SecureApplicationLauncher::detail
{
	struct LaunchApi {
		decltype(&::OpenProcessToken) openProcessToken{ &::OpenProcessToken };
		decltype(&::GetTokenInformation) getTokenInformation{ &::GetTokenInformation };
		decltype(&::GetThreadDesktop) getThreadDesktop{ &::GetThreadDesktop };
		decltype(&::OpenWindowStationW) openWindowStation{ &::OpenWindowStationW };
		decltype(&::CloseWindowStation) closeWindowStation{ &::CloseWindowStation };
		decltype(&::CreateProcessW) createProcess{ &::CreateProcessW };
		decltype(&::CloseHandle) closeHandle{ &::CloseHandle };
		WindowStationPolicy::detail::DaclApi dacl;
	};

	[[nodiscard]] std::expected<std::wstring, DWORD>
	GetUserSidString(const LaunchApi &api);
	// Creates a process without changing ACLs; the callback only borrows its handles.
	[[nodiscard]] std::expected<DWORD, LaunchError> CreateApplication(
		std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted,
		const LaunchApi &api);
	[[nodiscard]] std::expected<DWORD, LaunchError> LaunchApplication(
		HDESK desktop, std::wstring_view desktopName, std::wstring_view commandLine,
		const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
			&onStarted,
		const LaunchApi &api);
} // namespace DesktopRanger::SecureApplicationLauncher::detail
