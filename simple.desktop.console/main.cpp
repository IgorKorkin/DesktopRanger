#include <cstdio>

#include <Windows.h>

#include "simple_secure_desktop.h"

int main()
{
	const auto result = SimpleSecureDesktop::RunSimpleDesktop();
	if (result) {
		return EXIT_SUCCESS;
	}

	const auto &error = result.error();
	// The original Desktop can still be hidden after restoration failed. Do not
	// create another modal window here; the worker already notified the active Desktop.
	if (std::fprintf(stderr,
					 "Simple Desktop failed: operation=%lu, rollback=%lu, window=%lu, "
					 "visible=%lu, thread=%lu, notification=%lu.\n",
					 error.operationError.value_or(0), error.rollbackError.value_or(0),
					 error.windowCleanupError.value_or(0),
					 error.visibleRestoreError.value_or(0),
					 error.threadRestoreError.value_or(0),
					 error.notificationError.value_or(0)) < 0) {
		::OutputDebugStringW(L"Simple Desktop failed; stderr is unavailable.\n");
	}
	return EXIT_FAILURE;
}
