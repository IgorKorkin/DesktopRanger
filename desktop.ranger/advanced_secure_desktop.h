#ifndef __ADVANCED_SECURE_DESKTOP_H__
#define __ADVANCED_SECURE_DESKTOP_H__

#include <string>

#include <Windows.h>

namespace AdvancedSecureDesktop
{
	HDESK GetDesktopHandle();
	std::wstring GetDesktopName();

	std::wstring GetRandomDesktopName();
	// Clears only borrowed application state; never closes a Desktop handle.
	void ClearDesktopInformation() noexcept;

	// On success the caller owns SecretDesktopHandle; DefaultDesktopHandle is borrowed.
	bool CreateNewDesktop(IN std::wstring DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle);

	bool SwitchToDesktop(IN HDESK TargetDesktopHandle);

} // namespace AdvancedSecureDesktop

#endif // __ADVANCED_SECURE_DESKTOP_H__
