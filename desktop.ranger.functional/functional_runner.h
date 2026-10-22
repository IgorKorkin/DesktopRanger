#pragma once

#include "../desktop.ranger.core/desktop_lifecycle.h"

#include <string>

namespace DesktopRanger::Functional
{
	enum class Scenario { LaunchReady, ShutdownCleanup, ReadinessTimeout };
	enum class Evidence { Simulation, CurrentDesktopIntegration };

	// Reserved for a future VM adapter. Phase 5 rejects a non-null hook test and
	// never calls SetWindowsHookEx or claims to have tested protection against hooks.
	using SetWindowsHookExTest = std::expected<void, DWORD> (*)(DWORD processId,
																DWORD guiThreadId,
																DWORD timeoutMs);

	struct Options {
		std::wstring probePath;
		std::wstring desktopName;
		DWORD readyTimeoutMs{ 10000 };
		DWORD shutdownTimeoutMs{ 1000 };
		DWORD terminateTimeoutMs{ 1000 };
		SetWindowsHookExTest hookTest{};
	};

	struct Result {
		Scenario scenario{};
		Evidence evidence{};
		DWORD expectedError{};
		DWORD observedError{};
		DWORD cleanupError{};
		DWORD terminationError{};
		std::optional<DWORD> switchRollbackError;
		DesktopLifecycle::RestoreError restoreError{};
		DesktopLifecycle::RestoreError restoreRetryError{};
		DWORD processId{};
		ULONGLONG elapsedMs{};
		bool readyConfirmed{};
		bool readinessTimedOut{};
		bool stoppedConfirmed{};
		bool passed{};
	};

	// Only integration on the caller's current Desktop is available publicly.
	// No Desktop creation/switching, ACL changes or hook tests are performed.
	[[nodiscard]] Result RunScenario(Scenario scenario, const Options &options);
	[[nodiscard]] std::expected<std::wstring, DWORD> CurrentDesktopName();
	[[nodiscard]] std::string FormatResult(const Result &result);
} // namespace DesktopRanger::Functional
