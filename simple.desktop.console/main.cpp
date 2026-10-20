#include <format>

#include <Windows.h>

#include "simple_secure_desktop.h"

int main()
{
	const auto result = SimpleSecureDesktop::RunSimpleDesktop();
	if (result) {
		return EXIT_SUCCESS;
	}

	const auto &error = result.error();
	const auto code = error.operationError.value_or(error.visibleRestoreError.value_or(
		error.threadRestoreError.value_or(error.rollbackError.value_or(
			error.windowCleanupError.value_or(ERROR_GEN_FAILURE)))));
	const auto message = std::format(L"Simple Desktop failed: {}", code);
	::MessageBoxW(nullptr, message.c_str(), L"Simple Desktop", MB_OK | MB_ICONERROR);
	return EXIT_FAILURE;
}
