#ifndef __SIMPLE_SECURE_DESKTOP_H__
#define __SIMPLE_SECURE_DESKTOP_H__

#include <expected>
#include <optional>

#include <Windows.h>

namespace BasicDesktop
{
	bool SwitchToDesktop(IN HDESK TargetDesktopHandle);
}

namespace SimpleSecureDesktop
{
	struct RunError {
		std::optional<DWORD> operationError;
		std::optional<DWORD> rollbackError;
		std::optional<DWORD> windowCleanupError;
		std::optional<DWORD> visibleRestoreError;
		std::optional<DWORD> threadRestoreError;
		std::optional<DWORD> notificationError;
	};

	bool CreateNewDesktop(IN const wchar_t *DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle);

	[[nodiscard]] std::expected<void, DWORD>
	LaunchAppOnDesktop(IN const wchar_t *DesktopName, IN wchar_t *CommandLine);
	[[nodiscard]] std::expected<void, RunError> RunSimpleDesktop();

} // namespace SimpleSecureDesktop

#endif // __SIMPLE_SECURE_DESKTOP_H__
