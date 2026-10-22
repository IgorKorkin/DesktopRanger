#include "functional_runner.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t *argv[])
try {
	namespace Functional = DesktopRanger::Functional;
	std::array<wchar_t, 32768> executable{};
	const auto length = ::GetModuleFileNameW(nullptr, executable.data(),
											 static_cast<DWORD>(executable.size()));
	if (!length || length >= executable.size())
		return ERROR_INSUFFICIENT_BUFFER;
	auto probe =
		std::filesystem::path(executable.data()).parent_path() / L"test_probe.exe";
	std::filesystem::path report;
	for (int i = 1; i < argc; ++i) {
		if (std::wstring_view(argv[i]) == L"--probe" && i + 1 < argc)
			probe = argv[++i];
		else if (std::wstring_view(argv[i]) == L"--report" && i + 1 < argc)
			report = argv[++i];
		else {
			std::cerr << "Usage: functional_test_runner.exe [--probe test_probe.exe] "
						 "[--report path]\n";
			return ERROR_INVALID_PARAMETER;
		}
	}
	probe = std::filesystem::absolute(probe);
	if (probe.filename() != L"test_probe.exe" ||
		!std::filesystem::is_regular_file(probe)) {
		std::cerr << "Precompiled test_probe.exe was not found.\n";
		return ERROR_FILE_NOT_FOUND;
	}
	const auto desktopName = Functional::CurrentDesktopName();
	if (!desktopName)
		return static_cast<int>(desktopName.error() ? desktopName.error()
													: ERROR_GEN_FAILURE);
	std::ofstream file;
	if (!report.empty()) {
		file.open(report, std::ios::trunc);
		if (!file)
			return ERROR_WRITE_FAULT;
	}
	const auto write = [&](const std::string &line) {
		std::cout << line << '\n' << std::flush;
		if (file.is_open())
			file << line << '\n' << std::flush;
	};
	write("CURRENT_DESKTOP_INTEGRATION: no Desktop switching or ACL changes; "
		  "REAL_ISOLATION_TESTS=NOT_RUN; SetWindowsHookEx=NOT_RUN");
	Functional::Options options{ .probePath = probe.wstring(),
								 .desktopName = *desktopName };
	int failed{};
	for (const auto scenario :
		 { Functional::Scenario::LaunchReady, Functional::Scenario::ShutdownCleanup,
		   Functional::Scenario::ReadinessTimeout }) {
		auto caseOptions = options;
		if (scenario == Functional::Scenario::ReadinessTimeout) {
			caseOptions.readyTimeoutMs = 250;
		}
		const auto result = Functional::RunScenario(scenario, caseOptions);
		write(Functional::FormatResult(result));
		if (!result.passed)
			++failed;
	}
	write("SUMMARY: passed=" + std::to_string(3 - failed) +
		  " failed=" + std::to_string(failed));
	if (!std::cout || (file.is_open() && !file))
		return ERROR_WRITE_FAULT;
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
} catch (const std::exception &error) {
	std::cerr << "Runner error: " << error.what() << '\n';
	return EXIT_FAILURE;
}
