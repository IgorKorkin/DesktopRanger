#pragma once

#include "../process_elevation.h"

namespace DesktopRanger::ProcessElevation::detail
{
	struct ElevationApi {
		decltype(&::OpenProcessToken) openProcessToken{ &::OpenProcessToken };
		decltype(&::GetTokenInformation) getTokenInformation{ &::GetTokenInformation };
		decltype(&::CloseHandle) closeHandle{ &::CloseHandle };
	};

	[[nodiscard]] std::expected<bool, DWORD>
	IsProcessElevated(const ElevationApi &api) noexcept;
} // namespace DesktopRanger::ProcessElevation::detail
