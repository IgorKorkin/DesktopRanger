#include <cstdlib>

#include <Windows.h>

#include "../desktop.ranger/advanced_desktop_startup.h"
#include "../desktop.ranger/status_logger.h"

int main(int argc, char *[])
{
	if (DesktopRanger::ApplicationStartup::Initialize("DesktopRanger.Stable.log") !=
		EXIT_SUCCESS) {
		return EXIT_FAILURE;
	}
	if (argc != 1) {
		LOG_ERR(L"Usage: DesktopRanger.Stable.exe");
		return ERROR_INVALID_PARAMETER;
	}
	return DesktopRanger::ApplicationStartup::StartAdvancedDesktop();
}
