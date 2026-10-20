#include "paint_screen_border.h"

#include <utility>

namespace PaintScreenBorder
{
	namespace
	{
		constexpr int borderWidth = 5;
		constexpr COLORREF transparentColor = RGB(255, 0, 255);

		DWORD LastErrorOr(DWORD fallback)
		{
			const auto error = ::GetLastError();
			return error ? error : fallback;
		}

		LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			switch (msg) {
			case WM_PAINT: {
				PAINTSTRUCT ps{};
				const auto hdc = ::BeginPaint(hwnd, &ps);
				RECT client{};
				::GetClientRect(hwnd, &client);
				const auto background = ::CreateSolidBrush(transparentColor);
				::FillRect(hdc, &client, background);
				::DeleteObject(background);

				const auto color =
					static_cast<COLORREF>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				const auto pen = ::CreatePen(PS_SOLID, borderWidth, color);
				const auto oldPen = ::SelectObject(hdc, pen);
				const auto oldBrush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));
				const int half = borderWidth / 2;
				::Rectangle(hdc, half, half, client.right - half, client.bottom - half);
				::SelectObject(hdc, oldPen);
				::SelectObject(hdc, oldBrush);
				::DeleteObject(pen);
				::EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_DESTROY:
				return 0;
			default:
				return ::DefWindowProcW(hwnd, msg, wParam, lParam);
			}
		}

		HWND CreateScreenBorderWindow(const RECT &bounds, COLORREF borderColor)
		{
			static const wchar_t className[] = L"ScreenBorderWindow";
			static bool classRegistered = false;

			if (!classRegistered) {
				WNDCLASSEXW wc{};
				wc.cbSize = sizeof(WNDCLASSEXW);
				wc.lpfnWndProc = WndProc;
				wc.hInstance = ::GetModuleHandle(nullptr);
				wc.lpszClassName = className;
				wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(NULL_BRUSH));

				if (!::RegisterClassExW(&wc) &&
					::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
					return nullptr;
				}
				classRegistered = true;
			}

			HWND hwnd =
				::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
									  WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
								  className, L"", WS_POPUP, bounds.left, bounds.top,
								  bounds.right - bounds.left, bounds.bottom - bounds.top,
								  nullptr, nullptr, ::GetModuleHandle(nullptr), nullptr);
			if (!hwnd) {
				return nullptr;
			}

			::SetLastError(ERROR_SUCCESS);
			if (!::SetWindowLongPtrW(hwnd, GWLP_USERDATA, borderColor) &&
				::GetLastError() != ERROR_SUCCESS) {
				const auto error = ::GetLastError();
				::DestroyWindow(hwnd);
				::SetLastError(error);
				return nullptr;
			}
			if (!::SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY)) {
				const auto error = ::GetLastError();
				::DestroyWindow(hwnd);
				::SetLastError(error);
				return nullptr;
			}

			return hwnd;
		}

		struct BorderContext {
			std::vector<HWND> windows;
			COLORREF color{};
			DWORD error{};
		};

		BOOL CALLBACK CreateMonitorBorder(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
		{
			auto &context = *reinterpret_cast<BorderContext *>(parameter);
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			if (!::GetMonitorInfoW(monitor, &info)) {
				context.error = LastErrorOr(ERROR_GEN_FAILURE);
				return FALSE;
			}
			const auto window = CreateScreenBorderWindow(info.rcMonitor, context.color);
			if (!window) {
				context.error = LastErrorOr(ERROR_GEN_FAILURE);
				return FALSE;
			}
			try {
				context.windows.push_back(window);
			} catch (...) {
				context.error = ::DestroyWindow(window) ? ERROR_NOT_ENOUGH_MEMORY
														: LastErrorOr(ERROR_GEN_FAILURE);
				return FALSE;
			}
			return TRUE;
		}
	} // namespace

	std::expected<std::vector<HWND>, DWORD>
	CreateScreenBorderWindows(COLORREF borderColor)
	{
		if ((borderColor & 0x00FFFFFF) == transparentColor) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}
		BorderContext context{ .color = borderColor };
		if (!::EnumDisplayMonitors(nullptr, nullptr, CreateMonitorBorder,
								   reinterpret_cast<LPARAM>(&context))) {
			if (!context.error) {
				context.error = LastErrorOr(ERROR_GEN_FAILURE);
			}
		}
		if (context.error) {
			const auto cleanup = DestroyScreenBorderWindows(context.windows);
			return std::unexpected(cleanup ? context.error : cleanup.error());
		}
		if (context.windows.empty()) {
			return std::unexpected(ERROR_NOT_FOUND);
		}
		for (const auto window : context.windows) {
			::ShowWindow(window, SW_SHOW);
			::SendMessageW(window, WM_PAINT, 0, 0);
		}
		return std::move(context.windows);
	}

	std::expected<void, DWORD> DestroyScreenBorderWindows(std::span<const HWND> windows)
	{
		DWORD firstError{};
		for (auto window = windows.rbegin(); window != windows.rend(); ++window) {
			if (!::DestroyWindow(*window) && !firstError) {
				firstError = LastErrorOr(ERROR_GEN_FAILURE);
			}
		}
		if (firstError) {
			return std::unexpected(firstError);
		}
		return {};
	}
} // namespace PaintScreenBorder
