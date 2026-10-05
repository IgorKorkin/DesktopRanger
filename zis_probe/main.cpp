

#include <windows.h>
//
#include <sddl.h>

#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace
{

	std::wstring GetDesktopName()
	{
		HDESK desktop = GetThreadDesktop(GetCurrentThreadId());

		DWORD needed = 0;
		GetUserObjectInformationW(desktop, UOI_NAME, nullptr, 0, &needed);

		if (!needed)
			return L"<unknown>";

		std::vector<wchar_t> buffer(needed / sizeof(wchar_t) + 1);

		if (!GetUserObjectInformationW(
				desktop, UOI_NAME, buffer.data(),
				static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &needed)) {
			return L"<GetUserObjectInformation failed>";
		}

		return buffer.data();
	}

	bool IsAppContainer()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return false;
		}

		DWORD value = 0;
		DWORD bytes = 0;

		const BOOL ok = GetTokenInformation(token, TokenIsAppContainer, &value,
											sizeof(value), &bytes);

		CloseHandle(token);
		return ok && value != 0;
	}

	DWORD GetIntegrityRid()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			return 0;
		}

		DWORD bytes = 0;
		GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &bytes);

		std::vector<BYTE> buffer(bytes);

		if (!GetTokenInformation(token, TokenIntegrityLevel, buffer.data(), bytes,
								 &bytes)) {
			CloseHandle(token);
			return 0;
		}

		const auto til = reinterpret_cast<TOKEN_MANDATORY_LABEL *>(buffer.data());

		PSID sid = til->Label.Sid;
		const DWORD count = *GetSidSubAuthorityCount(sid);

		const DWORD rid = *GetSidSubAuthority(sid, count - 1);

		CloseHandle(token);
		return rid;
	}

	std::wstring IntegrityName(DWORD rid)
	{
		if (rid < SECURITY_MANDATORY_LOW_RID)
			return L"Untrusted";
		if (rid < SECURITY_MANDATORY_MEDIUM_RID)
			return L"Low";
		if (rid < SECURITY_MANDATORY_HIGH_RID)
			return L"Medium";
		if (rid < SECURITY_MANDATORY_SYSTEM_RID)
			return L"High";
		return L"System";
	}

	std::wstring TestDefaultDesktop()
	{
		// Request rights that would be dangerous for cross-desktop GUI access.
		HDESK desktop =
			OpenDesktopW(L"Default", 0, FALSE, DESKTOP_READOBJECTS | DESKTOP_HOOKCONTROL);

		if (desktop) {
			CloseDesktop(desktop);
			return L"ALLOWED (unexpected for the PoC)";
		}

		const DWORD error = GetLastError();

		std::wstringstream ss;
		ss << L"DENIED (expected), Win32 error " << error;
		return ss.str();
	}

} // namespace

int wmain()
{
	const bool appContainer = IsAppContainer();
	const DWORD integrity = GetIntegrityRid();

	std::wstringstream text;
	text << L"Hello from the ZIS PoC!\n\n"
		 << L"TokenIsAppContainer = " << (appContainer ? 1 : 0) << L"\n"
		 << L"Integrity level      = " << IntegrityName(integrity) << L" (RID 0x"
		 << std::hex << integrity << std::dec << L")\n"
		 << L"Current Desktop      = " << GetDesktopName() << L"\n"
		 << L"OpenDesktop(Default) = " << TestDefaultDesktop() << L"\n\n"
		 << L"Expected:\n"
		 << L"  AppContainer = 1\n"
		 << L"  Integrity    = Low\n"
		 << L"  Desktop      = DR_ZIS_POC\n"
		 << L"  Default      = DENIED\n\n"
		 << L"Click OK to return to the original desktop.";

	MessageBoxW(nullptr, text.str().c_str(), L"ZIS AppContainer + Desktop PoC",
				MB_OK | MB_ICONINFORMATION);

	return appContainer ? 0 : 2;
}
