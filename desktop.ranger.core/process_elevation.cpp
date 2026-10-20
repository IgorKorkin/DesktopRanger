#include "internal/process_elevation_internal.h"

#include <wil/resource.h>

namespace DesktopRanger::ProcessElevation
{
	std::expected<bool, DWORD> detail::IsProcessElevated(const ElevationApi &api) noexcept
	{
		HANDLE token{};
		if (!api.openProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return std::unexpected(::GetLastError());
		}
		auto closeToken = wil::scope_exit([&]() noexcept { api.closeHandle(token); });

		TOKEN_ELEVATION elevation{};
		DWORD size = sizeof(elevation);
		if (!api.getTokenInformation(token, TokenElevation, &elevation, sizeof(elevation),
									 &size)) {
			return std::unexpected(::GetLastError());
		}
		if (size < sizeof(elevation)) {
			return std::unexpected(ERROR_INVALID_DATA);
		}
		return elevation.TokenIsElevated != 0;
	}

	std::expected<bool, DWORD> IsProcessElevated() noexcept
	{
		return detail::IsProcessElevated({});
	}
} // namespace DesktopRanger::ProcessElevation
