
#include "../simple.desktop.console/simple_secure_desktop.h"
#include <Windows.h>

static constexpr wchar_t SecretDesktopName[] = L"SimpleSecretDesktop";

void MessageInformation(const wchar_t *lpText)
{
	::MessageBoxW(nullptr, lpText, SecretDesktopName,
				  MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SERVICE_NOTIFICATION);
}

HDESK DefaultDesktopHandle{};
HDESK SecretDesktopHandle{};
HWND border{};

DWORD WINAPI SimpleDesktopThread(LPVOID param)
{
	UNREFERENCED_PARAMETER(param);

	auto result = BasicDesktop::SwitchToDesktop(SecretDesktopHandle);
	if (result == false) {
		MessageInformation(L"Failed SwitchToDesktop(SecretDesktopHandle)");
		BasicDesktop::SwitchToDesktop(DefaultDesktopHandle);
		return 0;
	}

	border = PaintScreenBorder::CreateScreenBorderWindow();

	MessageInformation(L"You are on the SecretDesktop!\r\nClick [OK] to switch back");

	result = ::SwitchDesktop(DefaultDesktopHandle);
	if (result == false) {
		MessageInformation(L"Failed SwitchDesktop(DefaultDesktopHandle)");
		return 0;
	}

	return 0;
}

void SimpleDesktop()
{
	auto result = SimpleSecureDesktop::CreateNewDesktop(
		SecretDesktopName, DefaultDesktopHandle, SecretDesktopHandle);

	if (result == false) {
		MessageInformation(L"Failed CreateNewDesktop");
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

int main()
{
	SimpleDesktop();
}
