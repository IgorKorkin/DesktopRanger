#include <string>

#include <Aclapi.h>

#include "window_station_policy.h"

namespace DesktopRanger::WindowStationPolicy
{
	std::expected<UniqueHandle, DWORD> OpenStation(std::wstring_view stationName) noexcept
	{
		std::wstring nullTerminatedName{ stationName };
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

	std::expected<::ACL *, DWORD>
	GetDacl(const ::PSECURITY_DESCRIPTOR descriptor) noexcept
	{
		::ACL *dacl{};
		::BOOL daclPresent{};
		::BOOL daclDefaulted{};

		if (!::GetSecurityDescriptorDacl(static_cast<::PSECURITY_DESCRIPTOR>(descriptor),
										 &daclPresent, &dacl, &daclDefaulted)) {
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
	GetAclSizeInformation(const ::ACL *acl) noexcept
	{
		if (!acl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		::ACL_SIZE_INFORMATION info{};

		if (!::GetAclInformation(const_cast<::ACL *>(acl), &info, sizeof(info),
								 ::ACL_INFORMATION_CLASS::AclSizeInformation)) {
			return std::unexpected(::GetLastError());
		}

		return info;
	}

	std::expected<const ::ACE_HEADER *, DWORD> GetAceAt(const ::ACL *acl,
														DWORD aceIndex) noexcept
	{
		if (!acl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		void *rawAce{};
		if (!::GetAce(const_cast<::ACL *>(acl), aceIndex, &rawAce)) {
			return std::unexpected(::GetLastError());
		}

		return static_cast<const ::ACE_HEADER *>(rawAce);
	}

	std::expected<void, DWORD> AppendAce(::ACL *acl, const ::ACE_HEADER *ace) noexcept
	{
		if (!acl) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		if (!ace) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}

		if (!::AddAce(acl, ACL_REVISION, MAXDWORD, const_cast<::ACE_HEADER *>(ace),
					  ace->AceSize)) {
			return std::unexpected(::GetLastError());
		}
		return {};
	}

	std::expected<void, DWORD> CopyAces(const ::ACL *source, ::ACL *destination) noexcept
	{
		if (!source || !destination) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		auto info = GetAclSizeInformation(source);

		if (!info) {
			return std::unexpected(info.error());
		}

		for (DWORD aceIndex = 0; aceIndex < info->AceCount; ++aceIndex) {

			auto ace = GetAceAt(source, aceIndex);

			if (!ace) {
				return std::unexpected(ace.error());
			}

			auto appendResult = AppendAce(destination, ace.value());

			if (!appendResult) {
				return std::unexpected(appendResult.error());
			}
		}

		return {};
	}

} // namespace DesktopRanger::WindowStationPolicy