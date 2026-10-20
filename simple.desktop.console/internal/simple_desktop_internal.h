#pragma once

#include "../../desktop.ranger.core/desktop_lifecycle.h"
#include "../../desktop.ranger.gui/paint_screen_border.h"
#include "../simple_secure_desktop.h"

namespace SimpleSecureDesktop::detail
{
	namespace Lifecycle = DesktopRanger::DesktopLifecycle;

	// Only the existing Simple Desktop operations are substituted in unit tests.
	struct SimpleApi {
		decltype(&::CreateProcessW) createProcess{ &::CreateProcessW };
		decltype(&::WaitForInputIdle) waitForInputIdle{ &::WaitForInputIdle };
		decltype(&::CloseHandle) closeHandle{ &::CloseHandle };
		decltype(&::MessageBoxW) messageBox{ &::MessageBoxW };
		decltype(&Lifecycle::GetCurrentDesktop) getCurrentDesktop{
			&Lifecycle::GetCurrentDesktop
		};
		decltype(&Lifecycle::SwitchToDesktop) switchToDesktop{
			&Lifecycle::SwitchToDesktop
		};
		std::expected<void, Lifecycle::RestoreError> (*restoreDesktop)(
			HDESK, HDESK) noexcept { &Lifecycle::RestoreDesktop };
		decltype(&PaintScreenBorder::CreateScreenBorderWindows) createBorders{
			&PaintScreenBorder::CreateScreenBorderWindows
		};
		decltype(&PaintScreenBorder::DestroyScreenBorderWindows) destroyBorders{
			&PaintScreenBorder::DestroyScreenBorderWindows
		};
	};

	[[nodiscard]] std::expected<void, DWORD>
	LaunchAppOnDesktop(const wchar_t *desktopName, wchar_t *commandLine,
					   const SimpleApi &api);
	// Borrows all HDESKs. The owner keeps target alive until this worker has finished.
	void RunDesktopWorker(HDESK target, HDESK visibleOriginal, RunError &errors,
						  const SimpleApi &api) noexcept;
} // namespace SimpleSecureDesktop::detail
