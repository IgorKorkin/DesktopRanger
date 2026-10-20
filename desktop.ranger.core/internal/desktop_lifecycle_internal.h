#pragma once

#include "../desktop_lifecycle.h"

namespace DesktopRanger::DesktopLifecycle::detail
{
	struct DesktopApi {
		decltype(&::GetThreadDesktop) getThreadDesktop{ &::GetThreadDesktop };
		decltype(&::CreateDesktopW) createDesktop{ &::CreateDesktopW };
		decltype(&::SetThreadDesktop) setThreadDesktop{ &::SetThreadDesktop };
		decltype(&::SwitchDesktop) switchDesktop{ &::SwitchDesktop };
	};

	[[nodiscard]] std::expected<HDESK, DWORD>
	GetCurrentDesktop(const DesktopApi &api) noexcept;
	[[nodiscard]] std::expected<HDESK, DWORD>
	CreateSecureDesktopHandle(std::wstring_view name, const DesktopApi &api);

	// One adoption path for production and tests. Tests substitute only WIL's
	// compile-time deleter, so fake HDESKs never reach the real CloseDesktop.
	template <typename Owner>
	[[nodiscard]] std::expected<Owner, DWORD> CreateSecureDesktop(std::wstring_view name,
																  const DesktopApi &api)
	{
		auto handle = CreateSecureDesktopHandle(name, api);
		if (!handle) {
			return std::unexpected(handle.error());
		}
		return Owner{ handle.value() };
	}

	[[nodiscard]] std::expected<HDESK, SwitchError>
	SwitchToDesktop(HDESK target, const DesktopApi &api) noexcept;
	[[nodiscard]] std::expected<void, RestoreError>
	RestoreDesktop(HDESK original, const DesktopApi &api) noexcept;
	[[nodiscard]] std::expected<void, RestoreError>
	RestoreDesktop(HDESK visibleOriginal, HDESK threadOriginal,
				   const DesktopApi &api) noexcept;
} // namespace DesktopRanger::DesktopLifecycle::detail
