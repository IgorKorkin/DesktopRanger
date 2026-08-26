#include <cstring>
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

	std::expected<UniqueAcl, DWORD> CloneDacl(::ACL *source) noexcept
	{
		if (!source) {
			return std::unexpected(ERROR_INVALID_ACL);
		}

		::ACL_SIZE_INFORMATION info{};

		if (!::GetAclInformation(source, &info, sizeof(::ACL_SIZE_INFORMATION),
								 ::ACL_INFORMATION_CLASS::AclSizeInformation)) {
			return std::unexpected(::GetLastError());
		}

		const auto aclSize = info.AclBytesInUse + info.AclBytesFree;

		auto *copy = static_cast<::ACL *>(::LocalAlloc(LPTR, aclSize));

		if (!copy) {
			return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
		}

		std::memcpy(copy, source, aclSize);

		return UniqueAcl{ copy };
	}

} // namespace DesktopRanger::WindowStationPolicy