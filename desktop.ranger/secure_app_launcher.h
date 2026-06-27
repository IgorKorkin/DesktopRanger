#ifndef SECURE_APP_LAUNCHER_H
#define SECURE_APP_LAUNCHER_H

#include <string>

#include <windows.h>

namespace SecureApplicationLauncher
{
	void LaunchAppOnSecureDesktop(IN HDESK SecretDesktopHandle,
								  IN std::wstring DesktopName,
								  IN std::wstring CommandLine);

	void LaunchAppAndWaitNewWindow(IN std::wstring DesktopName,
								   IN std::wstring CommandLine);
} // namespace SecureApplicationLauncher

#endif // SECURE_APP_LAUNCHER_H