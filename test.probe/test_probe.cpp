#include <Windows.h>
#include <wil/resource.h>

#include <string_view>

namespace
{
	constexpr UINT confirmReady = WM_APP + 1;
	constexpr DWORD lifetimeMs = 15000;
	struct ProbeContext {
		HANDLE ready{};
		bool suppressReady{};
		DWORD error{};
	};
	DWORD LastError()
	{
		const auto error = ::GetLastError();
		return error ? error : ERROR_GEN_FAILURE;
	}
	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
	{
		if (message == WM_NCCREATE) {
			const auto create = reinterpret_cast<const CREATESTRUCTW *>(lp);
			::SetLastError(ERROR_SUCCESS);
			if (!::SetWindowLongPtrW(
					window, GWLP_USERDATA,
					reinterpret_cast<LONG_PTR>(create->lpCreateParams)) &&
				::GetLastError() != ERROR_SUCCESS) {
				return FALSE;
			}
		}
		if (message == confirmReady) {
			auto &context = *reinterpret_cast<ProbeContext *>(
				::GetWindowLongPtrW(window, GWLP_USERDATA));
			if (!context.suppressReady && !::SetEvent(context.ready)) {
				context.error = LastError();
				::PostQuitMessage(0);
			}
			return 0;
		}
		if (message == WM_CLOSE) {
			::PostQuitMessage(0); // Only this probe's own, dedicated message loop.
			return 0;
		}
		return ::DefWindowProcW(window, message, wp, lp);
	}
} // namespace

int wmain(int argc, wchar_t *argv[])
{
	if (argc != 5 || (std::wstring_view(argv[4]) != L"normal" &&
					  std::wstring_view(argv[4]) != L"no-ready")) {
		return ERROR_INVALID_PARAMETER;
	}
	wil::unique_handle ready{ ::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[1]) };
	if (!ready)
		return static_cast<int>(LastError());
	wil::unique_handle stop{ ::OpenEventW(SYNCHRONIZE, FALSE, argv[2]) };
	if (!stop)
		return static_cast<int>(LastError());
	wil::unique_handle stopped{ ::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]) };
	if (!stopped)
		return static_cast<int>(LastError());
	ProbeContext context{ ready.get(), std::wstring_view(argv[4]) == L"no-ready" };

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.hInstance = ::GetModuleHandleW(nullptr);
	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"DesktopRangerFunctionalProbe";
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	if (!::RegisterClassExW(&wc))
		return static_cast<int>(LastError());
	const auto window = ::CreateWindowExW(
		WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName,
		L"DesktopRanger test_probe (integration)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		CW_USEDEFAULT, 320, 120, nullptr, nullptr, wc.hInstance, &context);
	if (!window)
		return static_cast<int>(LastError());
	bool destroyed{};
	auto cleanup = wil::scope_exit([&]() noexcept {
		if (!destroyed)
			::DestroyWindow(window);
	});
	::ShowWindow(window, SW_SHOWNOACTIVATE);
	if (!::UpdateWindow(window))
		return static_cast<int>(LastError());
	// Readiness is acknowledged only when this message is dispatched by the loop.
	if (!::PostMessageW(window, confirmReady, 0, 0))
		return static_cast<int>(LastError());
	const auto deadline = ::GetTickCount64() + lifetimeMs;
	bool done{};
	while (!done) {
		const auto now = ::GetTickCount64();
		if (now >= deadline) {
			context.error = ERROR_TIMEOUT; // Self-cleanup even if the runner disappears.
			break;
		}
		const HANDLE handle = stop.get();
		const auto wait =
			::MsgWaitForMultipleObjectsEx(1, &handle, static_cast<DWORD>(deadline - now),
										  QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		if (wait == WAIT_OBJECT_0) {
			done = true;
		} else if (wait == WAIT_OBJECT_0 + 1) {
			MSG message{};
			// One message per iteration keeps the watchdog effective under queued input.
			if (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
				if (message.message == WM_QUIT)
					done = true;
				else {
					::TranslateMessage(&message);
					::DispatchMessageW(&message);
				}
			}
		} else {
			context.error = wait == WAIT_FAILED ? LastError() : ERROR_TIMEOUT;
			break;
		}
	}
	if (!::DestroyWindow(window))
		return static_cast<int>(LastError());
	destroyed = true;
	// This is a separate acknowledgment: the HWND is actually destroyed now.
	if (!::SetEvent(stopped.get()))
		return static_cast<int>(LastError());
	return static_cast<int>(context.error);
}
