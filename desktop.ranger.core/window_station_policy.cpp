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

	std::expected<UniqueSecurityDescriptor, DWORD> SnapshotDacl(HWINSTA station) noexcept
	{
		void *rawDescriptor{};

		const auto status =
			::GetSecurityInfo(station, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
							  nullptr, nullptr, nullptr, nullptr, &rawDescriptor);

		if (status != ERROR_SUCCESS) {
			return std::unexpected(status);
		}

		return UniqueSecurityDescriptor{ rawDescriptor };
	}

} // namespace DesktopRanger::WindowStationPolicy