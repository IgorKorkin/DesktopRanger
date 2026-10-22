#pragma once

#include "../../desktop.ranger.core/internal/desktop_lifecycle_internal.h"
#include "../../desktop.ranger.core/secure_application_launcher.h"
#include "../functional_runner.h"

namespace DesktopRanger::Functional::detail
{
	struct Signals {
		HANDLE ready{};
		HANDLE stop{};
		HANDLE stopped{};
		std::wstring_view readyName;
		std::wstring_view stopName;
		std::wstring_view stoppedName;
	};

	struct Api {
		decltype(&SecureApplicationLauncher::CreateApplication) launch{
			&SecureApplicationLauncher::CreateApplication
		};
		decltype(&::WaitForMultipleObjects) waitMany{ &::WaitForMultipleObjects };
		decltype(&::WaitForSingleObject) waitOne{ &::WaitForSingleObject };
		decltype(&::SetEvent) setEvent{ &::SetEvent };
		decltype(&::TerminateProcess) terminate{ &::TerminateProcess };
		decltype(&::GetExitCodeProcess) exitCode{ &::GetExitCodeProcess };
		decltype(&::GetTickCount64) now{ &::GetTickCount64 };
	};

	// Borrowed fake HDESKs and substituted Core WinAPI, supplied only by GoogleTest.
	// This fixture is not exposed by the executable or public integration entry point.
	struct SimulatedDesktop {
		HDESK target{};
		HDESK visibleOriginal{};
		DesktopLifecycle::detail::DesktopApi api;
	};

	[[nodiscard]] Result RunScenario(Scenario scenario, const Options &options,
									 const Signals &signals, Evidence evidence,
									 const Api &api,
									 const SimulatedDesktop *desktop = nullptr);
} // namespace DesktopRanger::Functional::detail
