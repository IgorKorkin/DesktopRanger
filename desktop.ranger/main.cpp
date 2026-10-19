#include <optional>
#include <print>
#include <thread>

#include <Windows.h>

#include "../simple.desktop.console/simple_secure_desktop.h"
#include "advanced_secure_desktop.h"
#include "control_window.h"
#include "desktop_lifecycle.h"
#include "secure_app_launcher.h"
#include "status_logger.h"
#include "winstation_patcher.h"

void MessageInformation(LPCSTR lpText)
{
	::MessageBoxA(nullptr, lpText, "MessageInformation",
				  MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SERVICE_NOTIFICATION);
}

namespace LaunchSimpleDesktop
{
	static constexpr wchar_t SecretDesktopName[] = L"SimpleSecretDesktop-null";

	HDESK DefaultDesktopHandle{};
	HDESK SecretDesktopHandle{};
	HWND border{};

	DWORD WINAPI SimpleDesktopThread(LPVOID param)
	{
		UNREFERENCED_PARAMETER(param);

		auto result = BasicDesktop::SwitchToDesktop(SecretDesktopHandle);
		if (result == false) {
			MessageInformation("Failed SwitchToDesktop(SecretDesktopHandle)");
			BasicDesktop::SwitchToDesktop(DefaultDesktopHandle);
			return 0;
		}

		border = PaintScreenBorder::CreateScreenBorderWindow();

		MessageInformation("You are on the SecretDesktop!\r\nClick [OK] to switch back");

		result = ::SwitchDesktop(DefaultDesktopHandle);
		if (result == false) {
			MessageInformation("Failed SwitchDesktop(DefaultDesktopHandle)");
			return 0;
		}

		return 0;
	}

	void SimpleDesktop()
	{
		auto result = SimpleSecureDesktop::CreateNewDesktop(
			SecretDesktopName, DefaultDesktopHandle, SecretDesktopHandle);

		if (result == false) {
			MessageInformation("Failed CreateNewDesktop");
			return;
		}

		/*
		Для обеспечения базовой функциональности изолированного рабочего стола
		запускаются отдельные системные утилиты (sndvol.exe), что позволяет
		обеспечить управление системой без инициализации полной оболочки
		Explorer.

		Избыточное ограничение ACCESS_MASK приводит к невозможности запуска ряда
		системных утилит, что демонстрирует строгую модель разграничения доступа
		к Desktop-объекту.

		*/
		wchar_t commandLineAppsFolder[] = L"explorer shell:AppsFolder";
		SimpleSecureDesktop::LaunchAppOnDesktop(SecretDesktopName, commandLineAppsFolder);

		// 	char commandLinesndvol[] = "sndvol.exe";
		// 	SimpleSecretDesktop::LaunchAppOnDesktop(SecretDesktopName,
		// 											commandLinesndvol);
		//
		// 	char commandLineexplorer[] = "explorer.exe";
		// 	SimpleSecretDesktop::LaunchAppOnDesktop(SecretDesktopName,
		// 											commandLineexplorer);

		const auto hThread =
			CreateThread(nullptr, 0, SimpleDesktopThread, SecretDesktopHandle, 0, 0);

		if (hThread) {
			::WaitForSingleObject(hThread, INFINITE);
			::CloseHandle(hThread);
		}

		::CloseWindow(border);
		::CloseDesktop(SecretDesktopHandle);
	}
} // namespace LaunchSimpleDesktop

namespace LaunchAdvancedDesktop
{
	namespace Lifecycle = DesktopRanger::DesktopLifecycle;

	struct WorkerContext {
		HDESK target{};
		HDESK original{};
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

		// The two HWND scopes below end before restoration runs on this worker.
		auto restoreDesktop = wil::scope_exit([&]() noexcept {
			const auto restored = Lifecycle::RestoreDesktop(context.original);
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

		const auto border = PaintScreenBorder::CreateScreenBorderWindow();
		if (!border) {
			context.workerError = ::GetLastError();
			if (!context.workerError) {
				context.workerError = ERROR_GEN_FAILURE;
			}
			return;
		}
		auto destroyBorder = wil::scope_exit([&]() noexcept {
			if (!::DestroyWindow(border)) {
				context.windowCleanupError = ::GetLastError();
				if (!context.windowCleanupError) {
					context.windowCleanupError = ERROR_GEN_FAILURE;
				}
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

			WorkerContext context{ desktop.get(), original };
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
} // namespace LaunchAdvancedDesktop

int CheckPriviledge()
{
	const auto elevated = WinstationPatcher::IsProcessElevated();
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

void test_OpenDesktop()
{
	static constexpr wchar_t simpleDesktop[] = L"simpleDesktop-null";
	static constexpr wchar_t advancedDesktop[] = L"advancedDesktop-null";
	static constexpr wchar_t unknownDesktop[] = L"unknownDesktop-null";

	HDESK DefaultDesktopHandle{};
	HDESK SecretDesktopHandle{};

	auto result = SimpleSecureDesktop::CreateNewDesktop(
		simpleDesktop, DefaultDesktopHandle, SecretDesktopHandle);

	result = AdvancedSecureDesktop::CreateNewDesktop(
		advancedDesktop, DefaultDesktopHandle, SecretDesktopHandle);

	auto hdesk = ::OpenDesktopW(simpleDesktop, 0, 0, DESKTOP_READOBJECTS);
	auto err = ::GetLastError();

	hdesk = ::OpenDesktopW(unknownDesktop, 0, 0, DESKTOP_READOBJECTS);
	err = ::GetLastError();

	hdesk = ::OpenDesktopW(advancedDesktop, 0, 0, DESKTOP_READOBJECTS);
	err = ::GetLastError();
}

int main()
{
#if 0
	test_OpenDesktop();
#endif // 0

	if (!StatusLogger::Init("DesktopRanger.log")) {
		MessageInformation("Fail to create a log file");
		return EXIT_FAILURE;
	}

	if (CheckPriviledge() != EXIT_SUCCESS) {
		return EXIT_FAILURE;
	}

	LOG_INFO(L"Running elevated.");

	return LaunchAdvancedDesktop::StartAdvancedDesktop();

	// ControlWindow::ShowContolWindow();

	// std::string DesktopName = "Default";

	// char CommandLine[] = "explorer shell:AppsFolder";
	//  char CommandLine[] = "C:/Program Files/Microsoft
	//  Office/root/Office16/POWERPNT.EXE";

	// SecureApplicationLauncher::LaunchAppAndWaitNewWindow(DesktopName, CommandLine);

	return EXIT_SUCCESS;
}
