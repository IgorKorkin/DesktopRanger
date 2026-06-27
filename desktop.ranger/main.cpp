#include <print>

#include <Windows.h>

#include "../simple.desktop.console/simple_secure_desktop.h"
#include "advanced_secure_desktop.h"
#include "control_window.h"
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
	HDESK DefaultDesktopHandle{};
	HDESK SecretDesktopHandle{};
	HWND border{};

	DWORD WINAPI AdvancedDesktopThread(LPVOID param)
	{
		UNREFERENCED_PARAMETER(param);

		auto result = BasicDesktop::SwitchToDesktop(SecretDesktopHandle);
		if (result == false) {
			MessageInformation("Failed SwitchToDesktop(SecretDesktopHandle)");
			BasicDesktop::SwitchToDesktop(DefaultDesktopHandle);
			return 0;
		}

		border = PaintScreenBorder::CreateScreenBorderWindow();

		ControlWindow::ShowContolWindow();

		// 		MessageInformation(
		// 			"You are on the SecretDesktop!\r\nClick [OK] to switch
		// back");

		result = ::SwitchDesktop(DefaultDesktopHandle);
		if (result == false) {
			MessageInformation("Failed SwitchDesktop(DefaultDesktopHandle)");
			return 0;
		}

		return 0;
	}

	int StartAdvancedDesktop()
	{
		const auto SecretDesktopName = AdvancedSecureDesktop::GetRandomDesktopName();

		LOG_INFO(std::format(L"SecretDesktopName = \"{}\"", SecretDesktopName));

		auto status = AdvancedSecureDesktop ::CreateNewDesktop(
			SecretDesktopName, DefaultDesktopHandle, SecretDesktopHandle);

		if (status == false) {
			LOG_ERR(L"Failed CreateNewDesktop");
			return 1;
		}

		LOG_INFO(std::format(L"Secret Desktop has been created! handle {:#x}",
							 reinterpret_cast<uintptr_t>(SecretDesktopHandle)));

		const auto thread =
			CreateThread(nullptr, 0, AdvancedDesktopThread, SecretDesktopHandle, 0, 0);

		if (thread) {
			::WaitForSingleObject(thread, INFINITE);
			::CloseHandle(thread);
		}

		::CloseWindow(border);
		::CloseDesktop(SecretDesktopHandle);

		return 0;
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

	LaunchAdvancedDesktop::StartAdvancedDesktop();

	// ControlWindow::ShowContolWindow();

	// std::string DesktopName = "Default";

	// char CommandLine[] = "explorer shell:AppsFolder";
	//  char CommandLine[] = "C:/Program Files/Microsoft
	//  Office/root/Office16/POWERPNT.EXE";

	// SecureApplicationLauncher::LaunchAppAndWaitNewWindow(DesktopName, CommandLine);

	return EXIT_SUCCESS;
}
