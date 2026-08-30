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
	std::expected<UniqueHandle, DWORD>
	OpenStation(std::wstring_view stationName = L"WinSta0") noexcept;

	[[nodiscard]]
	std::expected<UniqueSecurityDescriptor, DWORD>
	SnapshotDacl(::HWINSTA station) noexcept;

	[[nodiscard]]
	std::expected<::ACL *, DWORD> GetDacl(::PSECURITY_DESCRIPTOR descriptor) noexcept;

	[[nodiscard]] std::expected<UniqueAcl, DWORD> CreateAcl(DWORD size) noexcept;

	[[nodiscard]] std::expected<::ACL_SIZE_INFORMATION, DWORD>
	GetAclSizeInformation(const ::ACL *acl) noexcept;

	[[nodiscard]] std::expected<const ::ACE_HEADER *, DWORD>
	GetAceAt(const ::ACL *acl, DWORD aceIndex) noexcept;

	[[nodiscard]] std::expected<void, DWORD> AppendAce(::ACL *acl,
													   const ::ACE_HEADER *ace) noexcept;

	[[nodiscard]]
	std::expected<void, DWORD> CopyAces(const ::ACL *source, ::ACL *destination) noexcept;

} // namespace DesktopRanger::WindowStationPolicy
