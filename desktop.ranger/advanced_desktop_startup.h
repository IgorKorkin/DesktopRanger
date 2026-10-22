#pragma once

#include <filesystem>

namespace DesktopRanger::ApplicationStartup
{
	int Initialize(const std::filesystem::path &logFile);
	int StartAdvancedDesktop();
} // namespace DesktopRanger::ApplicationStartup
