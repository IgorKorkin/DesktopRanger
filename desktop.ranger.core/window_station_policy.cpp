#include <string>

#include <Aclapi.h>

#include "window_station_policy.h"
#include "internal/window_station_policy_internal.h"

namespace DesktopRanger::WindowStationPolicy
{
	static ::GENERIC_MAPPING mapping{
		.GenericRead = STANDARD_RIGHTS_READ | WINSTA_ENUMDESKTOPS | WINSTA_ENUMERATE |
					   WINSTA_READATTRIBUTES | WINSTA_READSCREEN,

		.GenericWrite = STANDARD_RIGHTS_WRITE | WINSTA_ACCESSCLIPBOARD |
						WINSTA_CREATEDESKTOP | WINSTA_WRITEATTRIBUTES,

		.GenericExecute =
			STANDARD_RIGHTS_EXECUTE | WINSTA_ACCESSGLOBALATOMS | WINSTA_EXITWINDOWS,

		.GenericAll = STANDARD_RIGHTS_REQUIRED | WINSTA_ACCESSCLIPBOARD |
					  WINSTA_ACCESSGLOBALATOMS | WINSTA_CREATEDESKTOP |
					  WINSTA_ENUMDESKTOPS | WINSTA_ENUMERATE | WINSTA_EXITWINDOWS |
					  WINSTA_READATTRIBUTES | WINSTA_READSCREEN | WINSTA_WRITEATTRIBUTES
	};

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
	detail::SnapshotDacl(::HWINSTA station, const DaclApi &api) noexcept
	{
		if (!station) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}
		void *rawDescriptor{};

		const auto status = api.getSecurityInfo(station, ::SE_OBJECT_TYPE::SE_WINDOW_OBJECT,
											  DACL_SECURITY_INFORMATION, nullptr, nullptr,
											  nullptr, nullptr, &rawDescriptor);
		if (status != ERROR_SUCCESS) {
			return std::unexpected(status);
		}

		return UniqueSecurityDescriptor{ rawDescriptor };
	}

	std::expected<::ACL *, DWORD> GetDacl(::PSECURITY_DESCRIPTOR descriptor) noexcept
	{
		if (!descriptor) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}
		::ACL *dacl{};
		::BOOL daclPresent{};
		::BOOL daclDefaulted{};

		if (!::GetSecurityDescriptorDacl(descriptor, &daclPresent, &dacl,
										 &daclDefaulted)) {
			return std::unexpected(::GetLastError());
		}

		if (!daclPresent) {
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

	template <typename TAce>
	[[nodiscard]] bool HasValidMask(const ::ACE_HEADER *ace) noexcept
	{
		return ace->AceSize >= FIELD_OFFSET(TAce, Mask) + sizeof(::ACCESS_MASK);
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

			auto aceHeader = const_cast<::ACE_HEADER *>(aceDestination.value());

			::ACCESS_MASK *mask{ nullptr };
			switch (aceHeader->AceType) {
			case ACCESS_ALLOWED_ACE_TYPE: {
				if (!HasValidMask<::ACCESS_ALLOWED_ACE>(aceHeader)) {
					return std::unexpected(ERROR_INVALID_ACL);
				}
				mask = &reinterpret_cast<::ACCESS_ALLOWED_ACE *>(aceHeader)->Mask;
				break;
			}
			case ACCESS_ALLOWED_OBJECT_ACE_TYPE: {
				if (!HasValidMask<::ACCESS_ALLOWED_OBJECT_ACE>(aceHeader)) {
					return std::unexpected(ERROR_INVALID_ACL);
				}
				mask = &reinterpret_cast<::ACCESS_ALLOWED_OBJECT_ACE *>(aceHeader)->Mask;
				break;
			}
			case ACCESS_ALLOWED_CALLBACK_ACE_TYPE: {
				if (!HasValidMask<::ACCESS_ALLOWED_CALLBACK_ACE>(aceHeader)) {
					return std::unexpected(ERROR_INVALID_ACL);
				}
				mask =
					&reinterpret_cast<::ACCESS_ALLOWED_CALLBACK_ACE *>(aceHeader)->Mask;
				break;
			}
			case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE: {
				if (!HasValidMask<::ACCESS_ALLOWED_CALLBACK_OBJECT_ACE>(aceHeader)) {
					return std::unexpected(ERROR_INVALID_ACL);
				}
				mask =
					&reinterpret_cast<::ACCESS_ALLOWED_CALLBACK_OBJECT_ACE *>(aceHeader)
						 ->Mask;
				break;
			}

			case ACCESS_ALLOWED_COMPOUND_ACE_TYPE:
				return std::unexpected(ERROR_NOT_SUPPORTED);
			default:
				break;
			}
			if (mask) {

				::MapGenericMask(mask, &mapping);
				*mask &= ~WINSTA_ENUMDESKTOPS;
			}
		}

		return destination;
	}

	static std::expected<void, DWORD> SetDacl(::HWINSTA station, ::ACL *dacl,
		const detail::DaclApi &api, SECURITY_INFORMATION information = DACL_SECURITY_INFORMATION) noexcept
	{
		const auto status =
			api.setSecurityInfo(station, SE_WINDOW_OBJECT, information,
							  nullptr, nullptr, dacl, nullptr);

		if (status != ERROR_SUCCESS) {
			return std::unexpected(status);
		}

		return {};
	}

	std::expected<void, DWORD> detail::ApplyDacl(::HWINSTA station, const ::ACL *dacl,
		const DaclApi &api) noexcept
	{
		if (!station || !dacl) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}

		return SetDacl(station, const_cast<::ACL *>(dacl), api);
	}

	std::expected<void, DWORD> detail::RestoreDacl(::HWINSTA station,
										   ::PSECURITY_DESCRIPTOR snapshot,
										   const DaclApi &api) noexcept
	{
		if (!station || !snapshot) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}

		auto dacl = GetDacl(snapshot);
		if (!dacl) {
			return std::unexpected(dacl.error());
		}

		SECURITY_DESCRIPTOR_CONTROL control{};
		DWORD revision{};
		if (!::GetSecurityDescriptorControl(snapshot, &control, &revision)) {
			return std::unexpected(::GetLastError());
		}
		const auto protection = (control & SE_DACL_PROTECTED)
			? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
		return SetDacl(station, dacl.value(), api, DACL_SECURITY_INFORMATION | protection);
	}

	std::expected<UniqueSecurityDescriptor, DWORD>
	detail::RestrictDacl(::HWINSTA station, const DaclApi &api) noexcept
	{
		if (!station) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}

		auto snapshot = SnapshotDacl(station, api);
		if (!snapshot) {
			return std::unexpected(snapshot.error());
		}

		auto originalDacl = GetDacl(snapshot->get());
		if (!originalDacl) {
			return std::unexpected(originalDacl.error());
		}

		// A NULL DACL grants unrestricted access and has no ACEs to restrict.
		// Converting it to an empty DACL would change the access policy to deny-all.
		if (!originalDacl.value()) {
			return std::unexpected(ERROR_NOT_SUPPORTED);
		}

		auto restrictedDacl = BuildRestrictedDacl(originalDacl.value());
		if (!restrictedDacl) {
			return std::unexpected(restrictedDacl.error());
		}

		auto result = ApplyDacl(station, restrictedDacl->get(), api);
		if (!result) {
			return std::unexpected(result.error());
		}

		return snapshot;
	}

	std::expected<UniqueSecurityDescriptor, DWORD> SnapshotDacl(::HWINSTA station) noexcept
	{
		return detail::SnapshotDacl(station, {});
	}

	std::expected<void, DWORD> ApplyDacl(::HWINSTA station, const ::ACL *dacl) noexcept
	{
		return detail::ApplyDacl(station, dacl, {});
	}

	std::expected<void, DWORD> RestoreDacl(::HWINSTA station,
		::PSECURITY_DESCRIPTOR snapshot) noexcept
	{
		return detail::RestoreDacl(station, snapshot, {});
	}

	std::expected<UniqueSecurityDescriptor, DWORD> RestrictDacl(::HWINSTA station) noexcept
	{
		return detail::RestrictDacl(station, {});
	}

	RestrictedSession::RestrictedSession(::HWINSTA station,
		RestoreFailureHandler onRestoreFailure, detail::DaclApi api) noexcept
		: station_(station), onRestoreFailure_(onRestoreFailure), api_(api)
	{
	}

	RestrictedSession::~RestrictedSession() noexcept
	{
		if (const auto result = Restore(); !result && onRestoreFailure_) {
			onRestoreFailure_(result.error());
		}
	}

	std::expected<void, DWORD> RestrictedSession::Restrict() noexcept
	{
		if (snapshot_) {
			return std::unexpected(ERROR_ALREADY_EXISTS);
		}
		auto snapshot = detail::RestrictDacl(station_, api_);
		if (!snapshot) {
			return std::unexpected(snapshot.error());
		}
		snapshot_ = std::move(snapshot.value());
		return {};
	}

	std::expected<void, DWORD> RestrictedSession::Restore() noexcept
	{
		if (!snapshot_) {
			return {};
		}
		auto result = detail::RestoreDacl(station_, snapshot_.get(), api_);
		if (result) {
			snapshot_.reset();
		}
		return result;
	}

} // namespace DesktopRanger::WindowStationPolicy
