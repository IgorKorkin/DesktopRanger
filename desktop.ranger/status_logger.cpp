#include <chrono>
#include <fstream>
#include <iostream>

#include "Windows.h"

#include "status_logger.h"

std::wofstream StatusLogger::Stream;
std::mutex StatusLogger::Mutex;

static constexpr wchar_t LogPath[] = L"C:/LOG";

bool StatusLogger::Init(const std::filesystem::path &fileName)
{
	std::lock_guard<std::mutex> lock(Mutex);

	if (Stream.is_open())
		Stream.close();

	const std::filesystem::path logPath(LogPath);
	if (!std::filesystem::exists(logPath)) {
		std::filesystem::create_directories(logPath);
	}

	auto filePath = logPath / fileName;

	Stream.open(filePath, std::ios::out | std::ios::app | std::ios::unitbuf);
	return Stream.is_open();
}

void StatusLogger::Log(Level level, std::wstring message, const std::source_location loc)
{
	std::lock_guard<std::mutex> lock(Mutex);

	auto fileName = std::filesystem::path(loc.file_name()).filename().wstring();

	std::wstring text = std::format(L"[{}] [{}] {} ({}:{})", Timestamp(),
									LevelToString(level), message, fileName, loc.line());

	if (level == Level::Error) {
		if (auto osMsg = LastErrorMessage(); !osMsg.empty())
			text += std::format(L" | OS error: {}", osMsg);
	}

	text += '\n';

	if (level == Level::Error)
		std::wcerr << text;
	else
		std::wcout << text;

	if (Stream.is_open()) {
		Stream << text;
		Flush();
	}
}

void StatusLogger::Close()
{
	std::lock_guard<std::mutex> lock(Mutex);
	if (Stream.is_open())
		Stream.close();
}

StatusLogger::~StatusLogger()
{
	Flush();
	Close();
}

void StatusLogger::Flush()
{
	if (Stream.is_open())
		Stream.flush();
}

std::wstring StatusLogger::Timestamp()
{
	// return std::format(L"{}", std::chrono::system_clock::now());

	using namespace std::chrono;

	auto now = std::chrono::system_clock::now();

	// Получаем локальное время в текущей временной зоне
	const auto zt = std::chrono::zoned_time{ std::chrono::current_zone(), now };

	// Форматируем в строку YYYY-MM-DD HH:MM:SS
	return std::format(L"{:%Y-%m-%d %H:%M:%S}", zt);
}

std::wstring StatusLogger::LastErrorMessage()
{
	const auto error = ::GetLastError();
	if (error == 0) {
		return {};
	}

	wchar_t *buffer{};
	::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr,
					 error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
					 reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

	std::wstring ws = buffer ? buffer : L"Unknown error";

	if (buffer) {
		::LocalFree(buffer);
	}

	ws.erase(ws.find_last_not_of(L"\r\n") + 1);

	// 	const auto sizeWithNullTerminator =
	// 		::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr,
	// nullptr);
	//
	// 	std::string msg(sizeWithNullTerminator - 1, 0);
	//
	// 	::WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, msg.data(),
	// sizeWithNullTerminator, 						  nullptr, nullptr);

	return std::format(L"{} (Code: {:#x})", ws, error);
}

const wchar_t *StatusLogger::LevelToString(Level level)
{
	switch (level) {
	case Level::Info:
		return L"INFO";
	case Level::Warning:
		return L"WARN";
	case Level::Error:
		return L"ERROR";
	default:
		return L"UNK";
	}
}