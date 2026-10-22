#include "advanced_desktop_startup.h"

#include <cstdlib>
#include <format>
#include <optional>
#include <thread>

#include <Windows.h>
#include <wil/resource.h>

#include "../desktop.ranger.gui/paint_screen_border.h"
#include "advanced_secure_desktop.h"
#include "control_window.h"
#include "desktop_lifecycle.h"
#include "process_elevation.h"
#include "status_logger.h"

namespace
{
	void MessageInformation(LPCSTR lpText)
	{
		::MessageBoxA(nullptr, lpText, "MessageInformation",
					  MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SERVICE_NOTIFICATION);
	}

	int CheckPriviledge()
	{
		const auto elevated = DesktopRanger::ProcessElevation::IsProcessElevated();
		if (!elevated) {
			LOG_ERR(L"IsProcessElevated fail");
			return EXIT_FAILURE;
		}

		if (!*elevated) {
			LOG_INFO(L"Application must be run as administrator");
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	}

} // namespace

namespace DesktopRanger::ApplicationStartup
{
	namespace Lifecycle = DesktopRanger::DesktopLifecycle;

	struct WorkerContext {
		HDESK target{};
		HDESK visibleOriginal{};
		HDESK threadOriginal{};
		std::optional<Lifecycle::SwitchError> switchFailure;
		Lifecycle::RestoreError restoreFailure{};
		DWORD workerError{};
		DWORD windowCleanupError{};
	};

	void LogDesktopError(const wchar_t *operation, DWORD error) noexcept
	{
		try {
			LOG_ERR(std::format(L"{} failed: {}", operation, error));
		} catch (...) {
			::OutputDebugStringW(L"DesktopRanger: Desktop lifecycle error.\n");
		}
	}

	void RunAdvancedDesktop(WorkerContext &context)
	{
		const auto threadOriginal = Lifecycle::GetCurrentDesktop();
		if (!threadOriginal) {
			context.switchFailure =
				Lifecycle::SwitchError{ Lifecycle::SwitchStep::GetOriginal,
										threadOriginal.error(), std::nullopt };
			LogDesktopError(L"Capture worker Desktop", threadOriginal.error());
			return;
		}
		context.threadOriginal = threadOriginal.value();
		auto restoreDesktop = wil::scope_exit([&]() noexcept {
			const auto restored = Lifecycle::RestoreDesktop(context.visibleOriginal,
															context.threadOriginal);
			if (!restored) {
				context.restoreFailure = restored.error();
				if (context.restoreFailure.switchError) {
					LogDesktopError(L"Restore visible Desktop",
									*context.restoreFailure.switchError);
				}
				if (context.restoreFailure.threadError) {
					LogDesktopError(L"Restore worker Desktop binding",
									*context.restoreFailure.threadError);
				}
			}
		});

		const auto switched = Lifecycle::SwitchToDesktop(context.target);
		if (!switched) {
			context.switchFailure = switched.error();
			LogDesktopError(L"Enter secure Desktop", context.switchFailure->error);
			if (context.switchFailure->rollbackError) {
				LogDesktopError(L"Rollback worker Desktop binding",
								*context.switchFailure->rollbackError);
			}
			return;
		}
		context.threadOriginal = switched.value();

		const auto borders = PaintScreenBorder::CreateScreenBorderWindows(RGB(0, 255, 0));
		if (!borders) {
			context.workerError = borders.error();
			return;
		}
		auto destroyBorders = wil::scope_exit([&]() noexcept {
			const auto destroyed =
				PaintScreenBorder::DestroyScreenBorderWindows(*borders);
			if (!destroyed) {
				context.windowCleanupError = destroyed.error();
				LogDesktopError(L"Destroy screen border on worker",
								context.windowCleanupError);
			}
		});
		const auto controlResult = ControlWindow::RunControlWindow();
		if (!controlResult) {
			context.workerError =
				controlResult.error() ? controlResult.error() : ERROR_GEN_FAILURE;
		}
	}

	DWORD WINAPI AdvancedDesktopThread(LPVOID param)
	{
		if (!param) {
			return ERROR_INVALID_PARAMETER;
		}
		auto &context = *static_cast<WorkerContext *>(param);
		try {
			RunAdvancedDesktop(context);
		} catch (...) {
			context.workerError = ERROR_UNHANDLED_EXCEPTION;
			LogDesktopError(L"Advanced Desktop worker", context.workerError);
		}
		return (context.workerError || context.windowCleanupError ||
				context.switchFailure || context.restoreFailure.switchError ||
				context.restoreFailure.threadError)
				   ? EXIT_FAILURE
				   : EXIT_SUCCESS;
	}

	int StartAdvancedDesktop()
	{
		try {
			const auto name = AdvancedSecureDesktop::GetRandomDesktopName();
			LOG_INFO(std::format(L"SecretDesktopName = \"{}\"", name));
			HDESK original{};
			HDESK rawDesktop{};
			if (!AdvancedSecureDesktop::CreateNewDesktop(name, original, rawDesktop)) {
				return EXIT_FAILURE;
			}
			Lifecycle::UniqueDesktop desktop{ rawDesktop };
			auto clearInformation = wil::scope_exit(
				[]() noexcept { AdvancedSecureDesktop::ClearDesktopInformation(); });
			LOG_INFO(std::format(L"Secret Desktop has been created! handle {:#x}",
								 reinterpret_cast<uintptr_t>(desktop.get())));

			WorkerContext context{};
			context.target = desktop.get();
			context.visibleOriginal = original;
			DWORD workerResult = EXIT_FAILURE;
			// Join is before borrowed application state and owned HDESK are released.
			// jthread also joins during ordinary exception unwinding.
			{
				std::jthread worker(
					[&] { workerResult = AdvancedDesktopThread(&context); });
				worker.join();
			}
			if (context.workerError) {
				LogDesktopError(L"Worker GUI", context.workerError);
			}
			return workerResult == EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
		} catch (...) {
			LogDesktopError(L"Create or join Advanced Desktop worker",
							ERROR_UNHANDLED_EXCEPTION);
			return EXIT_FAILURE;
		}
	}
} // namespace DesktopRanger::ApplicationStartup

namespace DesktopRanger::ApplicationStartup
{
	int Initialize(const std::filesystem::path &logFile)
	{
		bool initialized{};
		try {
			initialized = StatusLogger::Init(logFile);
		} catch (const std::filesystem::filesystem_error &) {
			initialized = false;
		}
		if (!initialized) {
			MessageInformation("Fail to create a log file");
			return EXIT_FAILURE;
		}

		if (CheckPriviledge() != EXIT_SUCCESS) {
			return EXIT_FAILURE;
		}

		LOG_INFO(L"Running elevated.");
		return EXIT_SUCCESS;
	}
} // namespace DesktopRanger::ApplicationStartup
