#pragma once

#include <expected>

#include <Windows.h>

namespace DesktopRanger::ProcessElevation
{
	[[nodiscard]] std::expected<bool, DWORD> IsProcessElevated() noexcept;
} // namespace DesktopRanger::ProcessElevation
