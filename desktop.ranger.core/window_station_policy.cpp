#include <string>

#include <Aclapi.h>

#include "window_station_policy.h"

namespace DesktopRanger::WindowStationPolicy
{
	std::expected<UniqueHandle, DWORD> Open(std::wstring_view name) noexcept
	{
		std::wstring nullTerminatedName{ name };
		UniqueHandle station{ ::OpenWindowStationW(nullTerminatedName.data(), FALSE,
												   READ_CONTROL | WRITE_DAC) };

		if (!station) {
			return std::unexpected(::GetLastError());
		}

		return station;
	}

	std::expected<UniqueSecurityDescriptor, DWORD>
	SnapshotDacl(::HWINSTA station) noexcept
	{
		void *rawDescriptor{};

		const auto status = ::GetSecurityInfo(station, ::SE_OBJECT_TYPE::SE_WINDOW_OBJECT,
											  DACL_SECURITY_INFORMATION, nullptr, nullptr,
											  nullptr, nullptr, &rawDescriptor);

		if (status != ERROR_SUCCESS) {
			return std::unexpected(status);
		}

		return UniqueSecurityDescriptor{ rawDescriptor };
	}

	std::expected<::ACL *, DWORD> GetDacl(::PSECURITY_DESCRIPTOR descriptor) noexcept
	{
		::ACL *dacl{};
		::BOOL daclPresent{};
		::BOOL daclDefaulted{};

		if (!::GetSecurityDescriptorDacl(descriptor, &daclPresent, &dacl,
										 &daclDefaulted)) {
			return std::unexpected(::GetLastError());
		}

		if (!daclPresent || !dacl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		return dacl;
	}

	std::expected<UniqueAcl, DWORD> CreateAcl(DWORD size) noexcept
	{
		UniqueAcl acl{ static_cast<::ACL *>(::LocalAlloc(LPTR, size)) };

		if (!acl) {
			return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
		}

		if (!::InitializeAcl(acl.get(), size, ACL_REVISION)) {
			return std::unexpected(::GetLastError());
		}

		return acl;
	}

	std::expected<::ACL_SIZE_INFORMATION, DWORD>
	GetAclSizeInformation(::ACL *acl) noexcept
	{
		if (!acl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		::ACL_SIZE_INFORMATION info{};

		if (!::GetAclInformation(acl, &info, sizeof(info),
								 ::ACL_INFORMATION_CLASS::AclSizeInformation)) {
			return std::unexpected(::GetLastError());
		}

		return info;
	}

	std::expected<::ACE_HEADER *, DWORD> GetAceAt(::ACL *acl, DWORD aceIndex) noexcept
	{
		if (!acl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		void *rawAce{};
		if (!::GetAce(acl, aceIndex, &rawAce)) {
			return std::unexpected(::GetLastError());
		}

		return static_cast<::ACE_HEADER *>(rawAce);
	}

} // namespace DesktopRanger::WindowStationPolicy