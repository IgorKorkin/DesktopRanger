

#include <windows.h>
//
#include <sddl.h>
#include <userenv.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <wil/resource.h>
#include <wil/result.h>

#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace
{
	enum class LaunchMode { Classic, Experimental, PsecProbe, Psec };

	constexpr DWORD_PTR kProcThreadAttributeSecurityEnvironment =
		35 | 0x00020000; // PROC_THREAD_ATTRIBUTE_INPUT

	using PFN_CreateProcessSecurityEnvironment =
		HRESULT(WINAPI *)(LPCVOID sandboxSpecification, DWORD sandboxSpecificationSize,
						  DWORD flags, HANDLE *processSecurityEnvironment);

	using PFN_QueryProcessSecurityEnvironmentSupport =
		HRESULT(WINAPI *)(UINT64 *supportFlags);

	using PFN_IsProcessSecurityEnvironmentVersionSupported =
		HRESULT(WINAPI *)(DWORD major, BOOLEAN *available, DWORD *minor);

	using PFN_CloseProcessSecurityEnvironment =
		void(WINAPI *)(HANDLE processSecurityEnvironment);

	using PFN_Experimental_CreateProcessInSandbox = BOOL(WINAPI *)(
		LPCWSTR applicationName, LPWSTR commandLine,
		LPSECURITY_ATTRIBUTES processAttributes, LPSECURITY_ATTRIBUTES threadAttributes,
		BOOL inheritHandles, DWORD creationFlags, LPVOID environment,
		LPCWSTR currentDirectory, LPSTARTUPINFOW startupInfo, LPCWSTR identity,
		LPCVOID sandboxSpecification, DWORD sandboxSpecificationSize,
		LPPROCESS_INFORMATION processInformation);

	static constexpr std::array<std::uint8_t, 40> kMinimalSandboxSpec{
		0x10, 0x00, 0x00, 0x00, // root table offset
		0x53, 0x42, 0x4F, 0x58, // "SBOX"

		// vtable
		0x08, 0x00, // vtable size
		0x0C, 0x00, // object size
		0x04, 0x00, // version field offset
		0x08, 0x00, // app_container field offset

		// SandboxSpec table
		0x08, 0x00, 0x00, 0x00, // vtable offset
		0x08, 0x00, 0x00, 0x00, // offset -> version string
		0x01,					// app_container = true
		0x00, 0x00, 0x00,		// padding

		// version = "0.1.0"
		0x05, 0x00, 0x00, 0x00, '0', '.', '1', '.', '0', '\0', 0x00, 0x00
	};

	constexpr wchar_t kProfileName[] = L"IgorKorkin.ZIS.PoC";
	constexpr wchar_t kDesktopName[] = L"DR_ZIS_POC";

	struct PsecLaunchContext {
		HMODULE processModel = nullptr;
		HANDLE environment = nullptr;
		PFN_CloseProcessSecurityEnvironment closeEnvironment = nullptr;
	};

	void ClosePsecContext(PsecLaunchContext &ctx)
	{
		if (ctx.environment && ctx.closeEnvironment) {
			ctx.closeEnvironment(ctx.environment);
			ctx.environment = nullptr;
		}

		if (ctx.processModel) {
			::FreeLibrary(ctx.processModel);
			ctx.processModel = nullptr;
		}

		ctx.closeEnvironment = nullptr;
	}

	std::string WideToUtf8(const std::wstring &value)
	{
		if (value.empty())
			return {};

		const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
												static_cast<int>(value.size()), nullptr,
												0, nullptr, nullptr);

		if (bytes <= 0)
			return {};

		std::string result(bytes, '\0');

		::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
							  result.data(), bytes, nullptr, nullptr);

		return result;
	}

	void AppendU16(std::vector<std::uint8_t> &b, std::uint16_t v)
	{
		b.push_back(static_cast<std::uint8_t>(v));
		b.push_back(static_cast<std::uint8_t>(v >> 8));
	}

	void AppendU32(std::vector<std::uint8_t> &b, std::uint32_t v)
	{
		b.push_back(static_cast<std::uint8_t>(v));
		b.push_back(static_cast<std::uint8_t>(v >> 8));
		b.push_back(static_cast<std::uint8_t>(v >> 16));
		b.push_back(static_cast<std::uint8_t>(v >> 24));
	}

	void PrintWin32Error(const wchar_t *where, DWORD error = GetLastError())
	{
		wchar_t *text = nullptr;
		::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
							 FORMAT_MESSAGE_IGNORE_INSERTS,
						 nullptr, error, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);

		std::wcerr << L"[!] " << where << L" failed. error=" << error;
		if (text) {
			std::wcerr << L" (" << text << L")";
			::LocalFree(text);
		}
		std::wcerr << L"\n";
	}

	void PrintHresult(const wchar_t *where, HRESULT hr)
	{
		std::wcerr << L"[!] " << where << L" failed. HRESULT=0x" << std::hex
				   << static_cast<unsigned long>(hr) << std::dec << L"\n";
	}

	bool SidToString(PSID sid, std::wstring &out)
	{
		LPWSTR stringSid = nullptr;
		if (!::ConvertSidToStringSidW(sid, &stringSid))
			return false;

		out.assign(stringSid);
		::LocalFree(stringSid);
		return true;
	}

	bool GetCurrentUserSidString(std::wstring &out)
	{
		HANDLE token = nullptr;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
			PrintWin32Error(L"OpenProcessToken");
			return false;
		}

		DWORD bytes = 0;
		::GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);

		std::vector<BYTE> buffer(bytes);
		if (!::GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
			PrintWin32Error(L"GetTokenInformation(TokenUser)");
			::CloseHandle(token);
			return false;
		}

		const auto user = reinterpret_cast<TOKEN_USER *>(buffer.data());
		const bool ok = SidToString(user->User.Sid, out);
		if (!ok)
			PrintWin32Error(L"ConvertSidToStringSidW(user)");

		CloseHandle(token);
		return ok;
	}

	HRESULT CreateOrOpenProfile(PSID *appContainerSid)
	{
		*appContainerSid = nullptr;

		HRESULT hr = ::CreateAppContainerProfile(kProfileName, L"ZIS PoC",
												 L"AppContainer + alternate Desktop PoC",
												 nullptr, 0, appContainerSid);

		if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
			hr = ::DeriveAppContainerSidFromAppContainerName(kProfileName,
															 appContainerSid);
		}

		return hr;
	}

	HDESK CreateLowIntegrityDesktop(const std::wstring &userSid,
									const std::wstring &appContainerSid,
									bool allowAllApplicationPackages)
	{
		// PoC policy:
		//   - current user: full access (broker must be able to switch desktops)
		//   - AppContainer SID: full access to THIS alternate desktop only
		//   - Low mandatory integrity label so the Low-IL AppContainer can use it
		//
		// The final research prototype should replace GA for the AppContainer
		// with the minimum Desktop rights actually required by the workload.
		std::wstring sddl = L"D:P"
							L"(A;;GA;;;SY)"
							L"(A;;GA;;;BA)"
							L"(A;;GA;;;" +
							userSid + L")";

		if (!appContainerSid.empty()) {
			sddl += L"(A;;GA;;;" + appContainerSid + L")";
		}

		if (allowAllApplicationPackages) {
			// Diagnostic allowance for PSEC PoC.
			sddl += L"(A;;GA;;;AC)";
		}

		sddl += L"S:(ML;;NW;;;LW)";

		::PSECURITY_DESCRIPTOR sd = nullptr;
		if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
			PrintWin32Error(L"ConvertStringSecurityDescriptorToSecurityDescriptorW");
			return nullptr;
		}

		SECURITY_ATTRIBUTES sa{};
		sa.nLength = sizeof(sa);
		sa.lpSecurityDescriptor = sd;
		sa.bInheritHandle = FALSE;

		::HDESK desktop =
			::CreateDesktopW(kDesktopName, nullptr, nullptr, 0, GENERIC_ALL, &sa);

		if (!desktop)
			PrintWin32Error(L"CreateDesktopW");

		::LocalFree(sd);
		return desktop;
	}

	std::filesystem::path GetSiblingProbe()
	{
		wchar_t path[MAX_PATH]{};
		const DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
		if (!n || n == MAX_PATH)
			return {};

		std::filesystem::path result(path);
		result.replace_filename(L"zis_probe.exe");
		return result;
	}

	bool CopyProbeIntoAppContainer(const std::wstring &appSidString,
								   std::filesystem::path &targetPath)
	{
		PWSTR folder = nullptr;
		const HRESULT hr = ::GetAppContainerFolderPath(appSidString.c_str(), &folder);

		if (FAILED(hr)) {
			PrintHresult(L"GetAppContainerFolderPath", hr);
			return false;
		}

		const std::filesystem::path source = GetSiblingProbe();
		if (source.empty() || !std::filesystem::exists(source)) {
			std::wcerr << L"[!] zis_probe.exe not found next to zis_launcher.exe\n";
			::CoTaskMemFree(folder);
			return false;
		}

		targetPath = std::filesystem::path(folder) / L"zis_probe.exe";
		::CoTaskMemFree(folder);

		std::error_code ec;
		std::filesystem::create_directories(targetPath.parent_path(), ec);

		if (!::CopyFileW(source.c_str(), targetPath.c_str(), FALSE)) {
			PrintWin32Error(L"CopyFileW(probe -> AppContainer folder)");
			return false;
		}

		return true;
	}

	bool LaunchExperimentalSandboxOnDesktop(const std::filesystem::path &imagePath,
											PROCESS_INFORMATION &pi)
	{
		HMODULE processModel =
			::LoadLibraryExW(L"processmodel.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

		if (!processModel) {
			PrintWin32Error(L"LoadLibraryExW(processmodel.dll)");
			return false;
		}

		const auto freeLibrary = wil::scope_exit([&] { ::FreeLibrary(processModel); });

		const auto createProcessInSandbox =
			reinterpret_cast<PFN_Experimental_CreateProcessInSandbox>(
				::GetProcAddress(processModel, "Experimental_CreateProcessInSandbox"));

		if (!createProcessInSandbox) {
			PrintWin32Error(L"GetProcAddress(Experimental_CreateProcessInSandbox)");
			return false;
		}

		std::wstring desktopPath = std::wstring(L"WinSta0\\") + kDesktopName;

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.lpDesktop = desktopPath.data();

		std::wcout << L"[+] Experimental_CreateProcessInSandbox\n"
				   << L"    Image:    " << imagePath.wstring() << L"\n"
				   << L"    Desktop:  " << desktopPath << L"\n"
				   << L"    Identity: " << kProfileName << L"\n";

		std::wstring commandLine = L"\"" + imagePath.wstring() + L"\"";

		const std::wstring currentDirectory = imagePath.parent_path().wstring();

		std::wcout << L"    Command:  " << commandLine << L"\n"
				   << L"    CWD:      " << currentDirectory << L"\n"
				   << L"    SBOX ptr: " << kMinimalSandboxSpec.data() << L"\n"
				   << L"    SBOX size:" << kMinimalSandboxSpec.size() << L"\n";

		::SetLastError(ERROR_SUCCESS);

		const BOOL ok =
			createProcessInSandbox(imagePath.c_str(),		   // applicationName
								   commandLine.data(),		   // commandLine - writable!
								   nullptr,					   // processAttributes
								   nullptr,					   // threadAttributes
								   FALSE,					   // inheritHandles
								   0,						   // creationFlags
								   nullptr,					   // environment
								   currentDirectory.c_str(),   // currentDirectory
								   &si,						   // startupInfo
								   kProfileName,			   // identity
								   kMinimalSandboxSpec.data(), // SBOX
								   static_cast<DWORD>(kMinimalSandboxSpec.size()), &pi);

		if (!ok) {
			const DWORD error = ::GetLastError();

			std::wcerr << L"[!] Experimental_CreateProcessInSandbox failed\n"
					   << L"    GetLastError = " << error << L" / 0x" << std::hex << error
					   << std::dec << L"\n";

			return false;
		}

		std::wcout << L"[+] Experimental sandbox process created\n"
				   << L"    PID = " << pi.dwProcessId << L"\n";

		return true;
	}

	bool LaunchAppContainerOnDesktop(PSID appSid, const std::filesystem::path &imagePath,
									 PROCESS_INFORMATION &pi)
	{
		SIZE_T attributeBytes = 0;
		::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);

		if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			PrintWin32Error(L"InitializeProcThreadAttributeList(size)");
			return false;
		}

		std::vector<BYTE> attributeStorage(attributeBytes);
		auto attributeList =
			reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());

		if (attributeList == nullptr) {
			return false;
		}

		if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {
			PrintWin32Error(L"InitializeProcThreadAttributeList");
			return false;
		}

		SECURITY_CAPABILITIES capabilities{};
		capabilities.AppContainerSid = appSid;
		capabilities.Capabilities = nullptr;
		capabilities.CapabilityCount = 0;
		capabilities.Reserved = 0;

		if (!::UpdateProcThreadAttribute(
				attributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
				&capabilities, sizeof(capabilities), nullptr, nullptr)) {
			PrintWin32Error(L"UpdateProcThreadAttribute(SECURITY_CAPABILITIES)");
			::DeleteProcThreadAttributeList(attributeList);
			return false;
		}

		std::wstring desktopPath = std::wstring(L"WinSta0\\") + kDesktopName;

		STARTUPINFOEXW si{};
		si.StartupInfo.cb = sizeof(si);
		si.StartupInfo.lpDesktop = desktopPath.data();
		si.lpAttributeList = attributeList;

		const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

		const BOOL ok =
			::CreateProcessW(imagePath.c_str(), nullptr, nullptr, nullptr, FALSE, flags,
							 nullptr, nullptr, &si.StartupInfo, &pi);

		if (!ok)
			PrintWin32Error(L"CreateProcessW(AppContainer + lpDesktop)");

		if (attributeList != nullptr) {
			::DeleteProcThreadAttributeList(attributeList);
		}

		return ok == TRUE;
	}

	int Cleanup()
	{
		const HRESULT hr = ::DeleteAppContainerProfile(kProfileName);

		if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
			std::wcout << L"Profile does not exist.\n";
			return 0;
		}

		if (FAILED(hr)) {
			PrintHresult(L"DeleteAppContainerProfile", hr);
			return 1;
		}

		std::wcout << L"Deleted AppContainer profile: " << kProfileName << L"\n";
		return 0;
	}

	int ProbeProcessSecurityEnvironment()
	{
		std::wcout << L"=== PSEC / CreateProcessSecurityEnvironment probe ===\n\n";

		HMODULE processModel =
			::LoadLibraryExW(L"processmodel.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

		if (!processModel) {
			PrintWin32Error(L"LoadLibraryExW(processmodel.dll)");
			return 1;
		}

		const auto freeLibrary = wil::scope_exit([&] { ::FreeLibrary(processModel); });

		const auto create = reinterpret_cast<PFN_CreateProcessSecurityEnvironment>(
			::GetProcAddress(processModel, "CreateProcessSecurityEnvironment"));

		const auto querySupport =
			reinterpret_cast<PFN_QueryProcessSecurityEnvironmentSupport>(
				::GetProcAddress(processModel, "QueryProcessSecurityEnvironmentSupport"));

		const auto versionSupport =
			reinterpret_cast<PFN_IsProcessSecurityEnvironmentVersionSupported>(
				::GetProcAddress(processModel,
								 "IsProcessSecurityEnvironmentVersionSupported"));

		const auto close = reinterpret_cast<PFN_CloseProcessSecurityEnvironment>(
			::GetProcAddress(processModel, "CloseProcessSecurityEnvironment"));

		std::wcout << L"processmodel.dll = " << reinterpret_cast<void *>(processModel)
				   << L"\n\n";

		std::wcout << L"CreateProcessSecurityEnvironment          = "
				   << reinterpret_cast<void *>(create) << L"\n";

		std::wcout << L"QueryProcessSecurityEnvironmentSupport    = "
				   << reinterpret_cast<void *>(querySupport) << L"\n";

		std::wcout << L"IsProcessSecurityEnvironmentVersionSupported = "
				   << reinterpret_cast<void *>(versionSupport) << L"\n";

		std::wcout << L"CloseProcessSecurityEnvironment           = "
				   << reinterpret_cast<void *>(close) << L"\n\n";

		const bool complete =
			create != nullptr && querySupport != nullptr && close != nullptr;

		std::wcout << L"Required PSEC export set: "
				   << (complete ? L"COMPLETE" : L"INCOMPLETE") << L"\n";

		if (!complete)
			return 2;

		//
		// Query optional native capabilities.
		//

		UINT64 supportFlags = 0;

		HRESULT hr = querySupport(&supportFlags);

		std::wcout << L"\nQueryProcessSecurityEnvironmentSupport:\n"
				   << L"  HRESULT = 0x" << std::hex << static_cast<unsigned long>(hr)
				   << std::dec << L"\n";

		if (SUCCEEDED(hr)) {
			std::wcout << L"  supportFlags = 0x" << std::hex << supportFlags << std::dec
					   << L"\n";

			std::wcout << L"  FS_DENY      = " << ((supportFlags & 0x1) ? L"yes" : L"no")
					   << L"\n";

			std::wcout << L"  FS_ENUMERATE = " << ((supportFlags & 0x4) ? L"yes" : L"no")
					   << L"\n";

			std::wcout << L"  NET_INGRESS  = " << ((supportFlags & 0x8) ? L"yes" : L"no")
					   << L"\n";
		}

		//
		// Version API is optional in current MXC.
		//

		if (versionSupport) {
			BOOLEAN available = FALSE;
			DWORD minor = 0;

			hr = versionSupport(1, // PSEC major 1
								&available, &minor);

			std::wcout << L"\nIsProcessSecurityEnvironmentVersionSupported(1):\n"
					   << L"  HRESULT   = 0x" << std::hex
					   << static_cast<unsigned long>(hr) << std::dec << L"\n"
					   << L"  available = " << (available ? L"TRUE" : L"FALSE") << L"\n"
					   << L"  max minor = " << minor << L"\n";
		} else {
			std::wcout << L"\nVersion-support export is absent "
						  L"(not fatal for this probe).\n";
		}

		return 0;
	}

	std::vector<std::uint8_t>
	BuildMinimalPsecV11(const std::filesystem::path &readOnlyPath)
	{
		const std::string utf8Path = WideToUtf8(readOnlyPath.wstring());

		std::vector<std::uint8_t> b;
		b.reserve(64 + utf8Path.size());

		//
		// FlatBuffer header
		//
		// root table starts at offset 24
		//

		AppendU32(b, 24);

		b.push_back('P');
		b.push_back('S');
		b.push_back('E');
		b.push_back('C');

		//
		// vtable starts at offset 8
		//
		// Fields through fs_read_only (#5):
		//
		// 0 version
		// 1 capabilities
		// 2 disallow_win32k_system_calls
		// 3 ui_restrictions
		// 4 fs_read_write
		// 5 fs_read_only
		//

		AppendU16(b, 16); // vtable size
		AppendU16(b, 12); // object size

		AppendU16(b, 4); // version
		AppendU16(b, 0); // capabilities
		AppendU16(b, 0); // disallow_win32k
		AppendU16(b, 0); // ui_restrictions
		AppendU16(b, 0); // fs_read_write
		AppendU16(b, 8); // fs_read_only

		//
		// Root table starts at offset 24.
		//

		AppendU32(b, 16); // table -> vtable distance

		//
		// SchemaVersion { major = 1, minor = 1 }
		//

		AppendU16(b, 1);
		AppendU16(b, 1);

		//
		// fs_read_only uoffset:
		// field is at offset 32
		// vector starts at offset 36
		//

		AppendU32(b, 4);

		//
		// vector<string>, one element
		//

		AppendU32(b, 1);

		//
		// vector element at offset 40;
		// string starts at 44
		//

		AppendU32(b, 4);

		//
		// FlatBuffer string
		//

		AppendU32(b, static_cast<std::uint32_t>(utf8Path.size()));

		b.insert(b.end(), utf8Path.begin(), utf8Path.end());

		b.push_back('\0');

		while ((b.size() % 4) != 0)
			b.push_back(0);

		return b;
	}

	bool LaunchPsecOnDesktop(const std::filesystem::path &imagePath,
							 PROCESS_INFORMATION &pi, PsecLaunchContext &ctx)
	{
		ctx = {};

		ctx.processModel =
			::LoadLibraryExW(L"processmodel.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

		if (!ctx.processModel) {
			PrintWin32Error(L"LoadLibraryExW(processmodel.dll)");
			return false;
		}

		const auto createEnvironment =
			reinterpret_cast<PFN_CreateProcessSecurityEnvironment>(
				::GetProcAddress(ctx.processModel, "CreateProcessSecurityEnvironment"));

		ctx.closeEnvironment = reinterpret_cast<PFN_CloseProcessSecurityEnvironment>(
			::GetProcAddress(ctx.processModel, "CloseProcessSecurityEnvironment"));

		if (!createEnvironment || !ctx.closeEnvironment) {

			PrintWin32Error(L"GetProcAddress(PSEC)");

			ClosePsecContext(ctx);
			return false;
		}

		//
		// Give native PSEC read-only access to the directory
		// containing zis_probe.exe.
		//

		const auto readOnlyPath = imagePath.parent_path();

		const auto psec = BuildMinimalPsecV11(readOnlyPath);

		std::wcout << L"[+] Building PSEC 1.1\n"
				   << L"    Read-only path: " << readOnlyPath.wstring() << L"\n"
				   << L"    PSEC size:      " << psec.size() << L"\n";

		HRESULT hr = createEnvironment(psec.data(), static_cast<DWORD>(psec.size()),
									   0, // PROCESS_SECURITY_ENVIRONMENT_FLAG_NONE
									   &ctx.environment);

		if (FAILED(hr)) {
			std::wcerr << L"[!] CreateProcessSecurityEnvironment failed\n"
					   << L"    HRESULT = 0x" << std::hex
					   << static_cast<unsigned long>(hr) << std::dec << L"\n";

			ClosePsecContext(ctx);
			return false;
		}

		std::wcout << L"[+] Process Security Environment created\n"
				   << L"    Handle: " << ctx.environment << L"\n";

		//
		// Build PROC_THREAD_ATTRIBUTE_SECURITY_ENVIRONMENT.
		//

		SIZE_T attributeBytes = 0;

		::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);

		if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			PrintWin32Error(L"InitializeProcThreadAttributeList(size)");

			ClosePsecContext(ctx);
			return false;
		}

		std::vector<BYTE> attributeStorage(attributeBytes);

		auto attributeList =
			reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());

		if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) {

			PrintWin32Error(L"InitializeProcThreadAttributeList");

			ClosePsecContext(ctx);
			return false;
		}

		const auto deleteAttributes =
			wil::scope_exit([&] { ::DeleteProcThreadAttributeList(attributeList); });

		//
		// Important:
		// lpValue points to the HANDLE value, not directly
		// to the object represented by that handle.
		//

		if (!::UpdateProcThreadAttribute(
				attributeList, 0, kProcThreadAttributeSecurityEnvironment,
				&ctx.environment, sizeof(ctx.environment), nullptr, nullptr)) {

			PrintWin32Error(L"UpdateProcThreadAttribute("
							L"SECURITY_ENVIRONMENT)");

			ClosePsecContext(ctx);
			return false;
		}

		std::wstring desktopPath = std::wstring(L"WinSta0\\") + kDesktopName;

		STARTUPINFOEXW si{};
		si.StartupInfo.cb = sizeof(si);
		si.StartupInfo.lpDesktop = desktopPath.data();

		si.lpAttributeList = attributeList;

		std::wstring commandLine = L"\"" + imagePath.wstring() + L"\"";

		std::wstring currentDirectory = imagePath.parent_path().wstring();

		std::wcout << L"[+] CreateProcessW inside PSEC\n"
				   << L"    Image:   " << imagePath.wstring() << L"\n"
				   << L"    Desktop: " << desktopPath << L"\n"
				   << L"    Command: " << commandLine << L"\n";

		const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

		const BOOL ok = ::CreateProcessW(imagePath.c_str(), commandLine.data(), nullptr,
										 nullptr, FALSE, flags, nullptr,
										 currentDirectory.c_str(), &si.StartupInfo, &pi);

		if (!ok) {
			PrintWin32Error(L"CreateProcessW(PSEC + lpDesktop)");

			ClosePsecContext(ctx);
			return false;
		}

		std::wcout << L"[+] PSEC process created\n"
				   << L"    PID = " << pi.dwProcessId << L"\n";

		//
		// Do NOT close ctx.environment here.
		//
		// It must remain alive while the process executes.
		//

		return true;
	}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	LaunchMode mode = LaunchMode::Classic;
	PsecLaunchContext psecContext{};

	for (int i = 1; i < argc; ++i) {
		if (::wcscmp(argv[i], L"--classic") == 0) {
			mode = LaunchMode::Classic;
		} else if (::wcscmp(argv[i], L"--experimental") == 0) {
			mode = LaunchMode::Experimental;
		} else if (::wcscmp(argv[i], L"--psec-probe") == 0) {
			mode = LaunchMode::PsecProbe;
		} else if (::wcscmp(argv[i], L"--psec") == 0) {
			mode = LaunchMode::Psec;
		} else if (::wcscmp(argv[i], L"--cleanup") == 0) {
			return Cleanup();
		} else {
			std::wcerr << L"Unknown option: " << argv[i] << L"\n\n"
					   << L"Usage:\n"
					   << L"  check_app_container.exe --classic\n"
					   << L"  check_app_container.exe --experimental\n"
					   << L"  check_app_container.exe --cleanup\n"
					   << L"  check_app_container.exe --psec-probe\n"
					   << L"  check_app_container.exe --psec\n";

			return 1;
		}
	}

	if (mode == LaunchMode::PsecProbe)
		return ProbeProcessSecurityEnvironment();

	std::wcout << L"ZIS PoC: stable AppContainer + alternate Desktop\n"
			   << L"Run this as a normal interactive user, not elevated.\n\n";

	PSID appSid = nullptr;
	HRESULT hr = CreateOrOpenProfile(&appSid);
	if (FAILED(hr)) {
		PrintHresult(L"Create/derive AppContainer profile", hr);
		return 1;
	}

	std::wstring appSidString;
	std::wstring userSidString;

	if (!SidToString(appSid, appSidString) || !GetCurrentUserSidString(userSidString)) {
		FreeSid(appSid);
		return 1;
	}

	std::wcout << L"[+] AppContainer SID: " << appSidString << L"\n";

	HDESK originalDesktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);

	if (!originalDesktop) {
		PrintWin32Error(L"OpenInputDesktop");
		FreeSid(appSid);
		return 1;
	}

	HDESK sandboxDesktop =
		CreateLowIntegrityDesktop(userSidString, appSidString, mode == LaunchMode::Psec);

	if (!sandboxDesktop) {
		CloseDesktop(originalDesktop);
		FreeSid(appSid);
		return 1;
	}

	std::wcout << L"[+] Created desktop: WinSta0\\" << kDesktopName << L"\n";

	std::filesystem::path probePath;

	if (mode == LaunchMode::Psec) {

		probePath = GetSiblingProbe();

		if (probePath.empty() || !std::filesystem::exists(probePath)) {

			std::wcerr << L"[!] zis_probe.exe not found.\n";

			CloseDesktop(sandboxDesktop);
			CloseDesktop(originalDesktop);
			FreeSid(appSid);

			return 1;
		}

		std::wcout << L"[+] PSEC probe image:\n"
				   << L"    " << probePath.wstring() << L"\n";
	} else {

		if (!CopyProbeIntoAppContainer(appSidString, probePath)) {
			CloseDesktop(sandboxDesktop);
			CloseDesktop(originalDesktop);
			FreeSid(appSid);

			return 1;
		}

		std::wcout << L"[+] Probe copied to AppContainer folder:\n"
				   << L"    " << probePath.wstring() << L"\n";
	}

	::PROCESS_INFORMATION pi{};

	bool launched = false;

	switch (mode) {

	case LaunchMode::Classic:
		std::wcout << L"\n=== CLASSIC MODE ===\n"
				   << L"CreateProcessW + "
					  L"PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES\n\n";

		launched = LaunchAppContainerOnDesktop(appSid, probePath, pi);
		break;

	case LaunchMode::Experimental:
		std::wcout << L"\n=== EXPERIMENTAL MODE ===\n"
				   << L"processmodel.dll!"
					  L"Experimental_CreateProcessInSandbox\n\n";

		launched = LaunchExperimentalSandboxOnDesktop(probePath, pi);
		break;

	case LaunchMode::Psec:

		std::wcout << L"\n=== PSEC / BASECONTAINER MODE ===\n"
				   << L"CreateProcessSecurityEnvironment +\n"
				   << L"PROC_THREAD_ATTRIBUTE_SECURITY_ENVIRONMENT\n\n";

		launched = LaunchPsecOnDesktop(probePath, pi, psecContext);

		break;
	}

	if (!launched) {
		std::wcerr << L"[!] Process launch failed.\n";

		CloseDesktop(sandboxDesktop);
		CloseDesktop(originalDesktop);
		FreeSid(appSid);

		return 1;
	}

	std::wcout << L"[+] Child PID: " << pi.dwProcessId << L"\n"
			   << L"[+] Switching to the ZIS desktop for up to 30 seconds...\n";

	if (!::SwitchDesktop(sandboxDesktop)) {
		PrintWin32Error(L"SwitchDesktop(ZIS)");
	}

	const DWORD wait = ::WaitForSingleObject(pi.hProcess, 30'000);

	// Always return the user to the original input desktop.
	if (!::SwitchDesktop(originalDesktop))
		PrintWin32Error(L"SwitchDesktop(original)");

	if (wait == WAIT_TIMEOUT) {
		std::wcerr << L"[!] Probe timed out after 30 seconds; terminating it.\n";

		::TerminateProcess(pi.hProcess, 0xDEAD);

		// Wait while hProcess is still valid.
		::WaitForSingleObject(pi.hProcess, 5000);
	} else if (wait == WAIT_FAILED) {
		PrintWin32Error(L"WaitForSingleObject");

		::TerminateProcess(pi.hProcess, 0xDEAD);
		::WaitForSingleObject(pi.hProcess, 5000);
	}

	DWORD exitCode = 0;

	if (::GetExitCodeProcess(pi.hProcess, &exitCode)) {
		std::wcout << L"[+] Probe exit code: " << exitCode << L"\n";
	}

	// Environment must outlive the child.
	if (mode == LaunchMode::Psec) {
		std::wcout << L"[+] Closing Process Security Environment\n";

		ClosePsecContext(psecContext);
	}

	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);
	::CloseDesktop(sandboxDesktop);
	::CloseDesktop(originalDesktop);
	::FreeSid(appSid);

	std::wcout << L"\nDone.\n"
			   << L"To remove the profile later:\n"
			   << L"  zis_launcher.exe --cleanup\n";

	return 0;
}
