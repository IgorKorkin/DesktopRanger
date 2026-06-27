#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>

class StatusLogger {
public:
	enum class Level { Info, Warning, Error };

	static bool Init(const std::filesystem::path &fileName);
	static void Log(Level level, std::wstring message,
					const std::source_location loc = std::source_location::current());
	static void Close();

	~StatusLogger();

private:
	static void Flush();
	static std::wstring Timestamp();
	static std::wstring LastErrorMessage();
	static const wchar_t *LevelToString(Level level);

	static std::wofstream Stream;
	static std::mutex Mutex;
};

#define LOG_INFO(msg)                                                                    \
	do {                                                                                 \
		StatusLogger::Log(StatusLogger::Level::Info, msg);                               \
	} while (false);

#define LOG_ERR(msg)                                                                     \
	do {                                                                                 \
		StatusLogger::Log(StatusLogger::Level::Error, msg);                              \
	} while (false);
