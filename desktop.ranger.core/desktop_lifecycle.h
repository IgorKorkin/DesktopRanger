#pragma once

#include <Windows.h>
#include <expected>
#include <optional>
#include <string_view>
#include <wil/resource.h>

namespace DesktopRanger::DesktopLifecycle
{
	using UniqueDesktop =
		wil::unique_any<HDESK, decltype(&::CloseDesktop), ::CloseDesktop>;

	enum class SwitchStep { GetOriginal, SetThread, VerifyThread, SwitchVisible };
	struct SwitchError {
		SwitchStep step;
		DWORD error;
		std::optional<DWORD> rollbackError;
	};
	struct RestoreError {
		// Presence means failure, including an API failure with GetLastError() == 0.
		std::optional<DWORD> switchError;
		std::optional<DWORD> threadError;
	};

	// Borrowed handle: never pass this result to CloseDesktop or UniqueDesktop.
	[[nodiscard]] std::expected<HDESK, DWORD> GetCurrentDesktop() noexcept;
	// Creates the existing deny-all, non-inheritable Desktop (D:P, GENERIC_ALL).
	[[nodiscard]] std::expected<UniqueDesktop, DWORD>
	CreateSecureDesktop(std::wstring_view name);
	// Call on a worker before creating windows/hooks. Returns its borrowed prior Desktop.
	// On failure after binding, attempts to restore the binding; retains both errors.
	[[nodiscard]] std::expected<HDESK, SwitchError>
	SwitchToDesktop(HDESK target) noexcept;
	// Destroy worker windows/hooks first. Always attempts visible restoration and
	// rebinding independently; neither operation is guaranteed to succeed.
	[[nodiscard]] std::expected<void, RestoreError>
	RestoreDesktop(HDESK original) noexcept;
	// Use separate borrowed handles when the caller and worker started on different
	// Desktops. SwitchDesktop restores visible state; SetThreadDesktop restores binding.
	[[nodiscard]] std::expected<void, RestoreError>
	RestoreDesktop(HDESK visibleOriginal, HDESK threadOriginal) noexcept;
} // namespace DesktopRanger::DesktopLifecycle
