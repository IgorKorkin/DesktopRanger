#include <string>

#include "desktop_lifecycle.h"
#include "internal/desktop_lifecycle_internal.h"
#include "security_descriptor.h"

namespace DesktopRanger::DesktopLifecycle
{
	std::expected<HDESK, DWORD> detail::GetCurrentDesktop(const DesktopApi &api) noexcept
	{
		const auto desktop = api.getThreadDesktop(::GetCurrentThreadId());
		if (!desktop) {
			return std::unexpected(::GetLastError());
		}
		return desktop;
	}

	std::expected<HDESK, DWORD> detail::CreateSecureDesktopHandle(std::wstring_view name,
																  const DesktopApi &api)
	{
		if (name.empty() || name.find(L'\0') != std::wstring_view::npos) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}
		// All allocations happen before CreateDesktop returns an owned resource.
		const std::wstring terminatedName(name);
		SecurityDescriptor descriptor(L"D:P");
		if (!descriptor.Initialized()) {
			return std::unexpected(::GetLastError());
		}
		SECURITY_ATTRIBUTES attributes{ .nLength = sizeof(SECURITY_ATTRIBUTES),
										.lpSecurityDescriptor =
											descriptor.GetDescriptor(),
										.bInheritHandle = FALSE };
		const auto desktop = api.createDesktop(terminatedName.c_str(), nullptr, nullptr,
											   0, GENERIC_ALL, &attributes);
		if (!desktop) {
			return std::unexpected(::GetLastError());
		}
		return desktop;
	}

	std::expected<HDESK, SwitchError>
	detail::SwitchToDesktop(HDESK target, const DesktopApi &api) noexcept
	{
		if (!target) {
			return std::unexpected(
				SwitchError{ SwitchStep::SetThread, ERROR_INVALID_HANDLE });
		}
		const auto original = detail::GetCurrentDesktop(api);
		if (!original) {
			return std::unexpected(
				SwitchError{ SwitchStep::GetOriginal, original.error() });
		}
		if (!api.setThreadDesktop(target)) {
			return std::unexpected(
				SwitchError{ SwitchStep::SetThread, ::GetLastError() });
		}
		const auto bound = detail::GetCurrentDesktop(api);
		SwitchError failure{ SwitchStep::VerifyThread, ERROR_INVALID_STATE };
		if (!bound) {
			failure.error = bound.error();
		} else if (bound.value() == target) {
			if (api.switchDesktop(target)) {
				return original.value();
			}
			failure = { SwitchStep::SwitchVisible, ::GetLastError() };
		}
		// Capture the primary error before the rollback can change GetLastError.
		if (!api.setThreadDesktop(original.value())) {
			failure.rollbackError = ::GetLastError();
		}
		return std::unexpected(failure);
	}

	std::expected<void, RestoreError>
	detail::RestoreDesktop(HDESK original, const DesktopApi &api) noexcept
	{
		if (!original) {
			return std::unexpected(
				RestoreError{ ERROR_INVALID_HANDLE, ERROR_INVALID_HANDLE });
		}
		RestoreError failure{};
		// Restore the visible desktop even if a remaining hook prevents rebinding.
		if (!api.switchDesktop(original)) {
			failure.switchError = ::GetLastError();
		}
		if (!api.setThreadDesktop(original)) {
			failure.threadError = ::GetLastError();
		} else {
			const auto bound = detail::GetCurrentDesktop(api);
			if (!bound) {
				failure.threadError = bound.error();
			} else if (bound.value() != original) {
				failure.threadError = ERROR_INVALID_STATE;
			}
		}
		if (failure.switchError || failure.threadError) {
			return std::unexpected(failure);
		}
		return {};
	}

	std::expected<HDESK, DWORD> GetCurrentDesktop() noexcept
	{
		return detail::GetCurrentDesktop({});
	}
	std::expected<UniqueDesktop, DWORD> CreateSecureDesktop(std::wstring_view name)
	{
		return detail::CreateSecureDesktop<UniqueDesktop>(name, {});
	}
	std::expected<HDESK, SwitchError> SwitchToDesktop(HDESK target) noexcept
	{
		return detail::SwitchToDesktop(target, {});
	}
	std::expected<void, RestoreError> RestoreDesktop(HDESK original) noexcept
	{
		return detail::RestoreDesktop(original, {});
	}
} // namespace DesktopRanger::DesktopLifecycle
