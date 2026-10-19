#pragma once

#include "../window_station_policy.h"
#include <Aclapi.h>

// Internal OS boundary shared by production code and in-memory failure tests.
// No global overrides: every session keeps its own pair of function pointers.
namespace DesktopRanger::WindowStationPolicy::detail
{
	struct DaclApi {
		decltype(&::GetSecurityInfo) getSecurityInfo{ &::GetSecurityInfo };
		decltype(&::SetSecurityInfo) setSecurityInfo{ &::SetSecurityInfo };
	};

	[[nodiscard]] std::expected<UniqueSecurityDescriptor, DWORD>
	SnapshotDacl(::HWINSTA station, const DaclApi &api) noexcept;
	[[nodiscard]] std::expected<void, DWORD> ApplyDacl(::HWINSTA station, const ::ACL *dacl,
		const DaclApi &api) noexcept;
	[[nodiscard]] std::expected<void, DWORD> RestoreDacl(::HWINSTA station,
		::PSECURITY_DESCRIPTOR snapshot, const DaclApi &api) noexcept;
	[[nodiscard]] std::expected<UniqueSecurityDescriptor, DWORD>
	RestrictDacl(::HWINSTA station, const DaclApi &api) noexcept;
}

namespace DesktopRanger::WindowStationPolicy
{
	// Borrows station: its handle must outlive this scope. Explicit Restore reports
	// errors and retains the snapshot on failure; destruction attempts Restore again.
	class RestrictedSession final {
	public:
		using RestoreFailureHandler = void (*)(DWORD) noexcept;
		explicit RestrictedSession(::HWINSTA station,
			RestoreFailureHandler onRestoreFailure, detail::DaclApi api = {}) noexcept;
		~RestrictedSession() noexcept;
		RestrictedSession(const RestrictedSession &) = delete;
		RestrictedSession &operator=(const RestrictedSession &) = delete;

		[[nodiscard]] std::expected<void, DWORD> Restrict() noexcept;
		[[nodiscard]] std::expected<void, DWORD> Restore() noexcept;

	private:
		::HWINSTA station_;
		RestoreFailureHandler onRestoreFailure_;
		detail::DaclApi api_;
		UniqueSecurityDescriptor snapshot_;
	};
}
