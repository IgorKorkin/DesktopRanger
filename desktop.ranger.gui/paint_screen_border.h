#pragma once

#include <Windows.h>

#include <expected>
#include <span>
#include <vector>

namespace PaintScreenBorder
{
	[[nodiscard]] std::expected<std::vector<HWND>, DWORD>
	CreateScreenBorderWindows(COLORREF borderColor);
	[[nodiscard]] std::expected<void, DWORD>
	DestroyScreenBorderWindows(std::span<const HWND> windows);
} // namespace PaintScreenBorder
