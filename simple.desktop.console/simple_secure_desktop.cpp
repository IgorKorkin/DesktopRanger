#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Aclapi.h>
#include <Sddl.h>
#include <tchar.h>

#include "simple_secure_desktop.h"

namespace
{
	/// Память из `ConvertSidToStringSidW` освобождается только через `LocalFree`.
	/// В C++26 для такого паттерна предусмотрен `std::unique_resource`; пока в MSVC STL
	/// его нет — тот же смысл даёт `unique_ptr` с deleter.
	struct LocalFreeDeleter {
		void operator()(wchar_t *p) const noexcept
		{
			if (p) {
				::LocalFree(p);
			}
		}
	};
	using SidLocalString = std::unique_ptr<wchar_t, LocalFreeDeleter>;
} // namespace

namespace BasicDesktop
{
	bool SwitchToDesktop(IN HDESK TargetDesktopHandle)
	{
		bool result{};
		if (::SetThreadDesktop(TargetDesktopHandle)) {
			const auto uThreadId = GetCurrentThreadId();
			if (::GetThreadDesktop(uThreadId) == TargetDesktopHandle) {
				if (::SwitchDesktop(TargetDesktopHandle)) {
					result = true;
				} else {
					::MessageBoxW(nullptr, L"Can't switch desktop", L"",
								  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
				}
			} else {
				::MessageBoxW(nullptr, L"We are on a wrong desktop", L"",
							  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
			}
		} else {
			auto err = ::GetLastError();
			::MessageBoxW(nullptr, L"Can't set desktop", L"",
						  MB_OK | MB_DEFAULT_DESKTOP_ONLY);
		}

		return result;
	}
} // namespace BasicDesktop

namespace SimpleSecureDesktop
{
	void LaunchAppOnDesktop(IN const wchar_t *DesktopName, IN wchar_t *CommandLine)
	{
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.lpDesktop = const_cast<wchar_t *>(DesktopName);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOW;
		PROCESS_INFORMATION pi{};

		const auto launched = ::CreateProcessW(nullptr, CommandLine, nullptr, nullptr, 0,
											   0, nullptr, nullptr, &si, &pi);
		if (launched) {
			::WaitForInputIdle(pi.hProcess, 200);
			::CloseHandle(pi.hThread);
			::CloseHandle(pi.hProcess);
		}
	}

	bool CreateNewDesktop(IN const wchar_t *DesktopName, OUT HDESK &DefaultDesktopHandle,
						  OUT HDESK &SecretDesktopHandle)
	{
		bool result = false;
		SecretDesktopHandle = nullptr;

		DefaultDesktopHandle = ::GetThreadDesktop(::GetCurrentThreadId());

		if (DefaultDesktopHandle == nullptr) {
			// DWORD Error = GetLastError();
			// TODO: print errr
			return false;
		}

		// Получаем SID текущего пользователя
		::HANDLE hToken = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			::MessageBoxA(nullptr, "Failed to open process token", "Error", MB_OK);
			return false;
		}

		DWORD dwSize = 0;
		::GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);

		std::vector<std::byte> tokenUserBuf(static_cast<std::size_t>(dwSize));
		auto *const pTokenUser = reinterpret_cast<TOKEN_USER *>(tokenUserBuf.data());

		if (!::GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
			::CloseHandle(hToken);
			::MessageBoxA(nullptr, "Failed to get token information", "Error", MB_OK);
			return false;
		}

		// Конвертируем SID в строку (буфер выделяет API → LocalFree в деструкторе
		// SidLocalString)
		wchar_t *sidRaw = nullptr;
		if (!::ConvertSidToStringSidW(pTokenUser->User.Sid, &sidRaw)) {
			::CloseHandle(hToken);
			::MessageBoxA(nullptr, "Failed to convert SID to string", "Error", MB_OK);
			return false;
		}
		SidLocalString sidString(sidRaw);
		::CloseHandle(hToken);

		//////////////////////////////////////////////////////////////////////////

		constexpr ACCESS_MASK deskFlags = DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU |
										  DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS |
										  DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE;

		// ЗАПРЕЩЕННЫЕ ФЛАГИ (не давать ни в коем случае):
		// DESKTOP_HOOKCONTROL     0x00002000 - разрешает хуки
		// DESKTOP_JOURNALRECORD   0x00004000 - запись действий
		// DESKTOP_JOURNALPLAYBACK 0x00008000 - воспроизведение
		// WRITE_DAC (0x40000)	Запрещает изменение прав
		// WRITE_OWNER (0x80000)	Запрещает смену владельца

		const std::wstring sddl = std::format(
			L"D:(A;;0x{:08X};;;{})", // текущий пользователь
			static_cast<unsigned>(deskFlags),
			sidString ? std::wstring_view{ sidString.get() } : std::wstring_view{});

		// Конвертируем SDDL в SECURITY_DESCRIPTOR
		PSECURITY_DESCRIPTOR securityDescriptor{};
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl.c_str(), SDDL_REVISION_1, &securityDescriptor, nullptr)) {
			::MessageBoxA(nullptr, "Failed to convert security descriptor", "Error",
						  MB_OK);
			return false;
		}

		SECURITY_ATTRIBUTES securityAttributes{};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = FALSE;
		securityAttributes.lpSecurityDescriptor = securityDescriptor;

		SecretDesktopHandle = ::CreateDesktopW(DesktopName, nullptr, nullptr, 0,
											   deskFlags, &securityAttributes);

		::LocalFree(securityDescriptor);

		return SecretDesktopHandle != nullptr;
	}

} // namespace SimpleSecureDesktop

namespace PaintScreenBorder
{
	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		static const int BORDER_WIDTH = 5;

		switch (msg) {
		case WM_PAINT: {

			PAINTSTRUCT ps{};
			HDC hdc = BeginPaint(hwnd, &ps);

			RECT rect{};
			GetClientRect(hwnd, &rect);

			const COLORREF magenta = RGB(255, 0, 255);
			HBRUSH bg = CreateSolidBrush(magenta);
			FillRect(hdc, &rect, bg);
			DeleteObject(bg);

			const int half = BORDER_WIDTH / 2;

			HPEN pen = CreatePen(PS_SOLID, BORDER_WIDTH, RGB(0, 255, 0));
			HGDIOBJ oldPen = SelectObject(hdc, pen);
			HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

			Rectangle(hdc, half, half, rect.right - half, rect.bottom - half);

			SelectObject(hdc, oldPen);
			SelectObject(hdc, oldBrush);
			DeleteObject(pen);

			EndPaint(hwnd, &ps);

		} break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			break;
		default:
			return ::DefWindowProc(hwnd, msg, wParam, lParam);
		}
		return 0;
	}

	HWND CreateScreenBorderWindow()
	{
		static const wchar_t className[] = L"ScreenBorderWindow";

		static bool classRegistered = false;

		if (!classRegistered) {
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.lpfnWndProc = WndProc;
			wc.hInstance = ::GetModuleHandle(nullptr);
			wc.lpszClassName = className;
			wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);

			::RegisterClassExW(&wc);
			classRegistered = true;
		}

		int x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
		int y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
		int width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
		int height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

		HWND hwnd =
			::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
								  WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
							  className, L"", WS_POPUP, x, y, width, height, nullptr,
							  nullptr, ::GetModuleHandle(nullptr), nullptr);

		::SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);

		::ShowWindow(hwnd, SW_SHOW);

		::SendMessage(hwnd, WM_PAINT, 0, 0);

		return hwnd;
	}
} // namespace PaintScreenBorder