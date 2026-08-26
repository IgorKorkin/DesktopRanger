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

	using UniqueSecurityDescriptor =
		wil::unique_any<PSECURITY_DESCRIPTOR, decltype(&::LocalFree), ::LocalFree>;

	[[nodiscard]]
	std::expected<UniqueSecurityDescriptor, DWORD> SnapshotDacl(HWINSTA station) noexcept;
} // namespace DesktopRanger::WindowStationPolicy
