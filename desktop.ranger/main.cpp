#include <cstdlib>
#include <format>
#include <string_view>

#include <Windows.h>

#include "../simple.desktop.console/simple_secure_desktop.h"
#include "advanced_desktop_startup.h"
#include "status_logger.h"

int main(int argc, char *argv[])
{
	if (DesktopRanger::ApplicationStartup::Initialize("DesktopRanger.log") !=
		EXIT_SUCCESS) {
		return EXIT_FAILURE;
	}

	if (argc == 2 && std::string_view(argv[1]) == "--simple") {
		const auto result = SimpleSecureDesktop::RunSimpleDesktop();
		if (!result) {
			const auto &error = result.error();
			if (error.operationError) {
				LOG_ERR(std::format(L"Simple Desktop operation failed: {}",
									*error.operationError));
			}
			if (error.rollbackError) {
				LOG_ERR(std::format(L"Simple Desktop rollback failed: {}",
									*error.rollbackError));
			}
			if (error.windowCleanupError) {
				LOG_ERR(std::format(L"Simple Desktop window cleanup failed: {}",
									*error.windowCleanupError));
			}
			if (error.visibleRestoreError) {
				LOG_ERR(std::format(L"Simple Desktop visible restore failed: {}",
									*error.visibleRestoreError));
			}
			if (error.threadRestoreError) {
				LOG_ERR(std::format(L"Simple Desktop worker restore failed: {}",
									*error.threadRestoreError));
			}
			if (error.notificationError) {
				LOG_ERR(std::format(L"Simple Desktop error notification failed: {}",
									*error.notificationError));
			}
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}
	if (argc != 1) {
		LOG_ERR(L"Usage: Desktop.Ranger.exe [--simple]");
		return ERROR_INVALID_PARAMETER;
	}

	return DesktopRanger::ApplicationStartup::StartAdvancedDesktop();
}
