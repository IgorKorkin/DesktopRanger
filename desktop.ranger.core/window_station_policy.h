#pragma once

#include <expected>
#include <string_view>

#include <Windows.h>

#include <wil/resource.h>

namespace DesktopRanger::WindowStationPolicy
{
	using UniqueHandle =
		wil::unique_any<::HWINSTA, decltype(&::CloseWindowStation), ::CloseWindowStation>;

	[[nodiscard]]
	std::expected<UniqueHandle, DWORD> Open(std::wstring_view name = L"WinSta0") noexcept;
} // namespace DesktopRanger::WindowStationPolicy
