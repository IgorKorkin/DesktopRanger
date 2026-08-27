#pragma once

#include <expected>
#include <string_view>

#include <Windows.h>

#include <wil/resource.h>

namespace DesktopRanger::WindowStationPolicy
{
	using UniqueHandle =
		wil::unique_any<::HWINSTA, decltype(&::CloseWindowStation), ::CloseWindowStation>;

	using UniqueSecurityDescriptor =
		wil::unique_any<::PSECURITY_DESCRIPTOR, decltype(&::LocalFree), ::LocalFree>;

	using UniqueAcl = wil::unique_any<::ACL *, decltype(&::LocalFree), ::LocalFree>;

	[[nodiscard]]
	std::expected<UniqueHandle, DWORD> Open(std::wstring_view name = L"WinSta0") noexcept;

	[[nodiscard]]
	std::expected<UniqueSecurityDescriptor, DWORD>
	SnapshotDacl(::HWINSTA station) noexcept;

	[[nodiscard]]
	std::expected<::ACL *, DWORD> GetDacl(::PSECURITY_DESCRIPTOR descriptor) noexcept;

	[[nodiscard]] std::expected<UniqueAcl, DWORD> CreateAcl(DWORD size) noexcept;

	[[nodiscard]] std::expected<::ACL_SIZE_INFORMATION, DWORD>
	GetAclSizeInformation(::ACL *acl) noexcept;

} // namespace DesktopRanger::WindowStationPolicy
