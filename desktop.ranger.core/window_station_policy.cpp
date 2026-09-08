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

	std::expected<UniqueAcl, DWORD> CreateAcl(DWORD size,
											  DWORD revision /* = ACL_REVISION*/) noexcept
	{
		UniqueAcl acl{ static_cast<::ACL *>(::LocalAlloc(LPTR, size)) };

		if (!acl) {
			return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
		}

		if (!::InitializeAcl(acl.get(), size, revision)) {
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

		if (!::AddAce(acl, acl->AclRevision, MAXDWORD, const_cast<::ACE_HEADER *>(ace),
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

		if (source == destination) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
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

	std::expected<UniqueAcl, DWORD> CreateEmptyAclLike(const ::ACL *source) noexcept
	{
		const auto info = GetAclSizeInformation(source);
		if (!info) {
			return std::unexpected(info.error());
		}

		return CreateAcl(info->AclBytesInUse, source->AclRevision);
	}

	std::expected<UniqueAcl, DWORD> BuildRestrictedDacl(const ::ACL *source) noexcept
	{
		const auto info = GetAclSizeInformation(source);
		if (!info) {
			return std::unexpected(info.error());
		}

		auto destination = CreateAcl(info->AclBytesInUse, source->AclRevision);
		if (!destination) {
			return std::unexpected(destination.error());
		}

		auto result = CopyAces(source, destination->get());
		if (!result) {
			return std::unexpected(result.error());
		}

		for (DWORD aceIndex = 0; aceIndex < info->AceCount; ++aceIndex) {

			auto aceDestination = GetAceAt(destination->get(), aceIndex);
			if (!aceDestination) {
				return std::unexpected(aceDestination.error());
			}

			auto aceHeader = const_cast<ACE_HEADER *>(aceDestination.value());

			switch (aceHeader->AceType) {
			case ACCESS_ALLOWED_ACE_TYPE:
			case ACCESS_ALLOWED_COMPOUND_ACE_TYPE:
			case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
			case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
			case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE: {
				auto allowed = reinterpret_cast<ACCESS_ALLOWED_ACE *>(aceHeader);
				if (allowed->Mask & WINSTA_ENUMDESKTOPS) {
					allowed->Mask &= ~WINSTA_ENUMDESKTOPS;
				}
				break;
			}
			default:
				break;
			}
		}

		return destination;
	}

} // namespace DesktopRanger::WindowStationPolicy