#include <filesystem>
#include <string>
#include <vector>

#include <Shlobj.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <windows.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

#include "control_window.h"

#include "advanced_secure_desktop.h"
#include "secure_app_launcher.h"

namespace ControlWindow
{
	// ------------------
	// Запуск произвольного exe
	// ------------------

	HFONT g_fontWin11{};
	HWND g_listView{};
	HWND g_ButtonBrowseApp{};
	HWND g_ButtonLaunchFromList{};
	HWND g_ButtonSwitchExit{};

	struct AppEntry {
		std::wstring name;
		std::wstring exePath;
		HICON icon;
	};

	std::vector<AppEntry> g_apps;

	void LaunchAppFromDisk()
	{
		wchar_t file[MAX_PATH]{};
		OPENFILENAMEW ofn{};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFile = file;
		ofn.nMaxFile = sizeof(file);
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

		if (GetOpenFileNameW(&ofn)) {
			SecureApplicationLauncher::LaunchAppOnSecureDesktop(
				AdvancedSecureDesktop::GetDesktopHandle(),
				AdvancedSecureDesktop::GetDesktopName(), file);
#if 0
			ShellExecuteA(nullptr, "open", file, nullptr, nullptr, SW_SHOW);
#endif
		}
	}

	// --------------------------
	// Запуск выбранного приложения
	// --------------------------
	void LaunchAppFromList()
	{
		auto idx = ::SendMessage(g_listView, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
		if (idx >= 0 && idx < (int)g_apps.size()) {

			SecureApplicationLauncher::LaunchAppOnSecureDesktop(
				AdvancedSecureDesktop::GetDesktopHandle(),
				AdvancedSecureDesktop::GetDesktopName(), g_apps[idx].exePath);

#if 0
						SecureApplicationLauncher::LaunchAppAndWaitNewWindow("Default",
																 g_apps[idx].exePath);

			ShellExecuteA(nullptr, "open", g_apps[idx].exePath.c_str(), nullptr, nullptr,
						  SW_SHOW);
#endif
		}
	}

	// --------------------------
	// Вспомогательная функция: добавить приложение в ListView
	// --------------------------
	void AddAppToListView(HWND lv, const AppEntry &app, int index)
	{
		// 		LVITEM lvi{};
		// 		lvi.mask = LVIF_TEXT | LVIF_IMAGE;
		// 		lvi.iItem = index;
		// 		lvi.iSubItem = 0;
		// 		lvi.pszText = const_cast<char *>(app.name.c_str());
		// 		lvi.iImage = index; // соответствие индексу в ImageList
		// 		ListView_InsertItem(lv, &lvi);
		//
		// 		ListView_SetItemText(lv, index, 1, const_cast<char
		// *>(app.exePath.c_str()));

		LVITEMW lvi{};
		lvi.mask = LVIF_TEXT | LVIF_IMAGE;
		lvi.iItem = index;
		lvi.iSubItem = 0;
		lvi.pszText = const_cast<LPWSTR>(app.name.c_str());
		lvi.iImage = index; // соответствие индексу в ImageList
		ListView_InsertItem(lv, &lvi);

		ListView_SetItemText(lv, index, 1, const_cast<LPWSTR>(app.exePath.c_str()));
	}

	// --------------------------
	// Инициализация ListView: колонки и ImageList
	// --------------------------
	void SetupListView(HWND hwnd)
	{
		RECT rc;
		GetClientRect(hwnd, &rc);

		g_listView = CreateWindowEx(0, WC_LISTVIEW, nullptr,
									WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 0,
									0, 0, 0, hwnd, nullptr, nullptr, nullptr);

		ListView_SetExtendedListViewStyle(
			g_listView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

		LVCOLUMN col{};
		col.mask = LVCF_TEXT | LVCF_WIDTH;
		col.cx = 200;
		wchar_t appNameCol[] = L"Application Name";
		col.pszText = appNameCol;
		ListView_InsertColumn(g_listView, 0, &col);

		col.cx = 500;
		wchar_t executableCol[] = L"Application Path";
		col.pszText = executableCol;
		ListView_InsertColumn(g_listView, 1, &col);

		// ImageList для иконок
		HIMAGELIST hImg = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 1, 1);
		ListView_SetImageList(g_listView, hImg, LVSIL_SMALL);
	}

	// --------------------------
	// Перечисление приложений из shell:AppsFolder
	// --------------------------
	void EnumerateAppsFolder()
	{
		g_apps.clear();

		IShellItem *psiApps = nullptr;
		CoInitializeEx(nullptr,
					   COINIT_APARTMENTTHREADED); // или COINIT_MULTITHREADED

		IShellFolder *psfDesktop = nullptr;
		HRESULT hr = ::SHGetDesktopFolder(&psfDesktop);
		if (SUCCEEDED(hr)) {
			LPITEMIDLIST pidlApps{};
			hr = SHGetSpecialFolderLocation(nullptr, CSIDL_COMMON_PROGRAMS,
											&pidlApps); // или CSIDL_PROGRAMS
			if (SUCCEEDED(hr)) {
				IShellFolder *psfApps = nullptr;
				hr = psfDesktop->BindToObject(pidlApps, nullptr, IID_PPV_ARGS(&psfApps));
				if (SUCCEEDED(hr)) {
					IEnumIDList *penum = nullptr;
					if (SUCCEEDED(
							psfApps->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &penum))) {
						LPITEMIDLIST pidl{};
						ULONG fetched{};
						int idx = 0;
						while (penum->Next(1, &pidl, &fetched) == S_OK) {
							STRRET str;
							if (SUCCEEDED(psfApps->GetDisplayNameOf(
									pidl, SHGDN_NORMAL | SHGDN_FORPARSING, &str))) {
								wchar_t szName[MAX_PATH];
								StrRetToBufW(&str, pidl, szName, MAX_PATH);

								AppEntry app{};
								app.name = std::filesystem::path(szName).stem().wstring();
								app.exePath = szName; // для наглядности
								app.icon = nullptr;	  // можно добавить извлечение
													  // иконки позднее
								g_apps.push_back(app);

								AddAppToListView(g_listView, app, idx++);
							}
							CoTaskMemFree(pidl);
						}
						penum->Release();
					}
				}
				CoTaskMemFree(pidlApps);
			}
			psfDesktop->Release();
		}

		CoUninitialize();
	}

	void ApplyFont(HWND hwnd)
	{
		SendMessage(hwnd, WM_SETFONT, (WPARAM)g_fontWin11, TRUE);
	}

	void Layout(HWND hwnd)
	{
		RECT rc;
		::GetClientRect(hwnd, &rc);

		int margin = 15;
		int spacing = 10;
		int buttonH = 30;
		int listHeight = rc.bottom - margin * 3 - buttonH; // оставляем место под кнопки

		// ListView занимает всё пространство сверху
		::MoveWindow(g_listView, margin, margin, rc.right - 2 * margin, listHeight, TRUE);

		// Кнопки внизу
		int totalButtons = 3;
		int buttonW =
			(rc.right - 2 * margin - spacing * (totalButtons - 1)) / totalButtons;
		int y = rc.bottom - margin - buttonH; // прижаты к низу

		::MoveWindow(g_ButtonBrowseApp, margin + buttonW + spacing, y, buttonW, buttonH,
					 TRUE);
		::MoveWindow(g_ButtonLaunchFromList, margin, y, buttonW, buttonH, TRUE);
		::MoveWindow(g_ButtonSwitchExit, margin + (buttonW + spacing) * 2, y, buttonW,
					 buttonH, TRUE);
	}

	enum ButtonID { LaunchApp = 1, LaunchFromList = 2, SwitchBack = 3 };

	// --------------------------
	LRESULT CALLBACK SecretDesktopWindowProcedure(HWND hwnd, UINT msg, WPARAM wParam,
												  LPARAM lParam)
	{
		switch (msg) {
		case WM_CREATE: {
			g_ButtonLaunchFromList = CreateWindowW(
				L"BUTTON", L"Run Selected", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0,
				0, 0, 0, hwnd, (HMENU)LaunchFromList, nullptr, nullptr);

			g_ButtonBrowseApp = CreateWindowW(
				L"BUTTON", L"Browse App…", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0,
				0, 0, hwnd, (HMENU)LaunchApp, nullptr, nullptr);

			g_ButtonSwitchExit = CreateWindowW(
				L"BUTTON", L"Switch Back", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0,
				0, 0, hwnd, (HMENU)SwitchBack, nullptr, nullptr);

			SendMessage(g_listView, WM_SETFONT, (WPARAM)g_fontWin11, TRUE);
			SendMessage(g_ButtonBrowseApp, WM_SETFONT, (WPARAM)g_fontWin11, TRUE);
			SendMessage(g_ButtonLaunchFromList, WM_SETFONT, (WPARAM)g_fontWin11, TRUE);
			SendMessage(g_ButtonSwitchExit, WM_SETFONT, (WPARAM)g_fontWin11, TRUE);

			Layout(hwnd);
			SetupListView(hwnd);
			EnumerateAppsFolder();
			break;
		}

		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
			case LaunchApp:
				LaunchAppFromDisk();
				break;
			case LaunchFromList:
				LaunchAppFromList();
				break;
			case SwitchBack:
				PostQuitMessage(0);
				break;
			}
			break;
		}

		case WM_NOTIFY: {
			LPNMHDR pnmh = (LPNMHDR)lParam;
			if (pnmh->hwndFrom == g_listView && pnmh->code == NM_DBLCLK) {
				LaunchAppFromList();
			}
			break;
		}

		case WM_SIZE: {
			Layout(hwnd);
			break;
		}

		case WM_DESTROY: {
			PostQuitMessage(0);
			break;
		}

		case WM_CLOSE: {
			break;
		}

		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
		}
		return 0;
	}

	void ShowContolWindow()
	{
		INITCOMMONCONTROLSEX icc{};
		icc.dwSize = sizeof(icc);
		icc.dwICC = ICC_WIN95_CLASSES;
		InitCommonControlsEx(&icc);

		HINSTANCE hInstance = ::GetModuleHandle(nullptr);

		WNDCLASS wc{};
		wc.lpfnWndProc = SecretDesktopWindowProcedure;
		wc.hInstance = hInstance;
		wc.lpszClassName = L"Win11Launcher";
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		RegisterClass(&wc);

		g_fontWin11 =
			::CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
						  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
						  DEFAULT_PITCH | FF_SWISS, L"Segoe UI Variable");

		const int w = 700, h = 360;
		HWND controlWindow = ::CreateWindowExW(
			0, L"Win11Launcher", L"Secret Desktop Launcher", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, hInstance, nullptr);

		DWORD exStyle = ::GetWindowLong(controlWindow, GWL_EXSTYLE);
		::SetWindowLong(controlWindow, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);

		// Remove close button
		HMENU systemMenu = ::GetSystemMenu(controlWindow, FALSE);
		::DeleteMenu(systemMenu, SC_CLOSE, MF_BYCOMMAND);

		::ShowWindow(controlWindow, SW_SHOW);
		::UpdateWindow(controlWindow);

		MSG message{};
		int result{};
		while ((result = ::GetMessageA(&message, nullptr, 0, 0)) != 0) {
			if (result == -1)
				break;

			::TranslateMessage(&message);
			::DispatchMessageA(&message);
		}
	}

} // namespace ControlWindow