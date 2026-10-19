#ifndef CONTROL_WINDOW_H
#define CONTROL_WINDOW_H

#include <Windows.h>
#include <expected>

namespace ControlWindow
{
	void ShowContolWindow();
	// Runs and destroys the control HWND on the calling worker thread.
	[[nodiscard]] std::expected<void, DWORD> RunControlWindow();
} // namespace ControlWindow

#endif
