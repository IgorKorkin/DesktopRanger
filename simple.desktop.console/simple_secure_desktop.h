#ifndef __SIMPLE_SECURE_DESKTOP_H__
#define __SIMPLE_SECURE_DESKTOP_H__

#include <Windows.h>

namespace BasicDesktop
{
	bool SwitchToDesktop(IN HDESK TargetDesktopHandle);
}

namespace SimpleSecureDesktop
{
	bool CreateNewDesktop(IN const wchar_t *DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle);

	void LaunchAppOnDesktop(IN const wchar_t *DesktopName, IN wchar_t *CommandLine);

} // namespace SimpleSecureDesktop

namespace PaintScreenBorder
{
	HWND CreateScreenBorderWindow();
}
#endif // __SIMPLE_SECURE_DESKTOP_H__
