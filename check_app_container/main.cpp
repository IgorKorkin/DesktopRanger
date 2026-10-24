

#include <windows.h>
//
#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>

#include <array>
#include <clocale>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <wil/resource.h>
#include <wil/result.h>

#include "filesystem/FilePolicy.h"

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
	constexpr wchar_t kCpisFileProfile[] = L"IgorKorkin.ZIS.CPIS.FileTest";
	constexpr wchar_t kDesktopName[] = L"DR_ZIS_POC";

	struct FileTestOptions {
		bool allowSecrets = false;
		bool denyOverridesGrant = false;
		bool noUi = false;
		bool specOnly = false;
		bool noFilesystemPolicy = false;
		std::vector<std::uint8_t> specification;
	};

	struct PsecLaunchContext {
		HMODULE processModel = nullptr;
		HANDLE environment = nullptr;
		PFN_CloseProcessSecurityEnvironment closeEnvironment = nullptr;
		std::vector<std::uint8_t> specification;
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
		ctx.specification.clear();
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

	std::wstring QuotePath(const std::filesystem::path &path);

	bool
	LaunchExperimentalSandboxOnDesktop(const std::filesystem::path &imagePath,
									   PROCESS_INFORMATION &pi,
									   const std::filesystem::path &fileTestRoot = {},
									   const FileTestOptions *fileTest = nullptr)
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
		if (fileTest)
			std::wcout << L"API available: CPIS export present\n";
		const auto *specData =
			fileTest ? fileTest->specification.data() : kMinimalSandboxSpec.data();
		const auto specSize =
			fileTest ? fileTest->specification.size() : kMinimalSandboxSpec.size();
		const auto *identity = fileTest ? kCpisFileProfile : kProfileName;

		std::wstring desktopPath = std::wstring(L"WinSta0\\") + kDesktopName;

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.lpDesktop = desktopPath.data();

		std::wcout << L"[+] Experimental_CreateProcessInSandbox\n"
				   << L"    Image:    " << imagePath.wstring() << L"\n"
				   << L"    Desktop:  " << desktopPath << L"\n"
				   << L"    Identity: " << identity << L"\n";

		std::wstring commandLine = L"\"" + imagePath.wstring() + L"\"";
		if (fileTest) {
			commandLine =
				QuotePath(imagePath) + L" --file-test " + QuotePath(fileTestRoot) +
				(fileTest->allowSecrets ? L" baseline cpis" : L" sandbox cpis") +
				(fileTest->noUi ? L"" : L" --messagebox");
		}

		const std::wstring currentDirectory = imagePath.parent_path().wstring();

		std::wcout << L"    Command:  " << commandLine << L"\n"
				   << L"    CWD:      " << currentDirectory << L"\n"
				   << L"    SBOX ptr: " << static_cast<const void *>(specData) << L"\n"
				   << L"    SBOX size:" << specSize << L"\n";

		::SetLastError(ERROR_SUCCESS);

		const BOOL ok =
			createProcessInSandbox(imagePath.c_str(),  // applicationName
								   commandLine.data(), // commandLine - writable!
								   nullptr,			   // processAttributes
								   nullptr,			   // threadAttributes
								   FALSE,			   // inheritHandles
								   fileTest ? CREATE_SUSPENDED : 0,
								   nullptr,					 // environment
								   currentDirectory.c_str(), // currentDirectory
								   &si,						 // startupInfo
								   identity, specData, static_cast<DWORD>(specSize), &pi);

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

	std::wstring QuotePath(const std::filesystem::path &path)
	{
		const auto value = path.wstring();
		std::wstring result = L"\"" + value;
		// Escape trailing backslashes before the closing command-line quote.
		for (size_t i = value.size(); i > 0 && value[i - 1] == L'\\'; --i)
			result += L'\\';
		return result + L"\"";
	}

	bool LaunchAppContainerOnDesktop(PSID appSid, const std::filesystem::path &imagePath,
									 PROCESS_INFORMATION &pi,
									 const std::filesystem::path &fileTestRoot)
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

		std::wstring commandLine;
		const std::wstring currentDirectory = imagePath.parent_path().wstring();
		if (!fileTestRoot.empty()) {
			commandLine = QuotePath(imagePath) + L" --file-test " +
						  QuotePath(fileTestRoot) + L" sandbox classic --messagebox";
			std::wcout << L"[+] File test command: " << commandLine << L"\n";
		}

		const BOOL ok = ::CreateProcessW(
			imagePath.c_str(), commandLine.empty() ? nullptr : commandLine.data(),
			nullptr, nullptr, FALSE, flags, nullptr,
			fileTestRoot.empty() ? nullptr : currentDirectory.c_str(), &si.StartupInfo,
			&pi);

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
		std::wcout << L"Experimental_CreateProcessInSandbox = "
				   << reinterpret_cast<void *>(::GetProcAddress(
						  processModel, "Experimental_CreateProcessInSandbox"))
				   << L"\n";

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
	BuildMinimalPsecV11(const std::filesystem::path &readOnlyPath,
						bool noFilesystemPolicy = false)
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

		AppendU16(b, 4);						  // version
		AppendU16(b, 0);						  // capabilities
		AppendU16(b, 0);						  // disallow_win32k
		AppendU16(b, 0);						  // ui_restrictions
		AppendU16(b, 0);						  // fs_read_write
		AppendU16(b, noFilesystemPolicy ? 0 : 8); // fs_read_only

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

		AppendU32(b, noFilesystemPolicy ? 0 : 4);
		if (noFilesystemPolicy)
			return b; // No filesystem fields or path vector.

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
							 PROCESS_INFORMATION &pi, PsecLaunchContext &ctx,
							 const std::filesystem::path &fileTestRoot = {},
							 const FileTestOptions *fileTest = nullptr,
							 bool noFilesystemPolicy = false)
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
		if (fileTest) {
			std::wcout << L"API available: PSEC create/close exports present\n";
			const auto query =
				reinterpret_cast<PFN_QueryProcessSecurityEnvironmentSupport>(
					::GetProcAddress(ctx.processModel,
									 "QueryProcessSecurityEnvironmentSupport"));
			if (!query) {
				PrintWin32Error(
					L"GetProcAddress(QueryProcessSecurityEnvironmentSupport)");
				ClosePsecContext(ctx);
				return false;
			}
			UINT64 support = 0;
			const HRESULT queryHr = query(&support);
			std::wcout << L"QueryProcessSecurityEnvironmentSupport: HRESULT=0x"
					   << std::hex << static_cast<unsigned long>(queryHr) << L" flags=0x"
					   << support << std::dec << L"\n";
			if (FAILED(queryHr) || (!fileTest->allowSecrets && !(support & 0x1))) {
				std::wcerr << L"Policy not created: native fs_deny support not "
							  L"confirmed. No fallback.\n";
				ClosePsecContext(ctx);
				return false;
			}
			const auto version =
				reinterpret_cast<PFN_IsProcessSecurityEnvironmentVersionSupported>(
					::GetProcAddress(ctx.processModel,
									 "IsProcessSecurityEnvironmentVersionSupported"));
			if (version) {
				BOOLEAN available = FALSE;
				DWORD minor = 0;
				const HRESULT versionHr = version(1, &available, &minor);
				std::wcout << L"PSEC version query: HRESULT=0x" << std::hex
						   << static_cast<unsigned long>(versionHr) << std::dec
						   << L" available=" << static_cast<unsigned>(available)
						   << L" maxMinor=" << minor << L"\n";
				if (FAILED(versionHr) || !available || minor < 1) {
					std::wcerr << L"Policy not created: PSEC 1.1 not "
								  L"supported/confirmed. No fallback.\n";
					ClosePsecContext(ctx);
					return false;
				}
			} else {
				std::wcout << L"Version query absent; Create must validate PSEC 1.1.\n";
			}
		}

		//
		// Give native PSEC read-only access to the directory
		// containing zis_probe.exe.
		//

		const auto readOnlyPath = imagePath.parent_path();

		ctx.specification = fileTest
								? fileTest->specification
								: BuildMinimalPsecV11(readOnlyPath, noFilesystemPolicy);
		const auto &psec = ctx.specification;

		std::wcout << L"[+] Building PSEC 1.1\n"
				   << L"    Read-only path: " << readOnlyPath.wstring() << L"\n"
				   << L"    PSEC size:      " << psec.size() << L"\n";
		if (noFilesystemPolicy)
			std::wcout << L"    Filesystem fields: absent (diagnostic control)\n";

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
		if (fileTest) {
			commandLine =
				QuotePath(imagePath) + L" --file-test " + QuotePath(fileTestRoot) +
				(fileTest->allowSecrets ? L" baseline psec" : L" sandbox psec") +
				(fileTest->noUi ? L"" : L" --messagebox");
		}

		std::wstring currentDirectory = imagePath.parent_path().wstring();

		std::wcout << L"[+] CreateProcessW inside PSEC\n"
				   << L"    Image:   " << imagePath.wstring() << L"\n"
				   << L"    Desktop: " << desktopPath << L"\n"
				   << L"    Command: " << commandLine << L"\n";

		const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
							(fileTest ? CREATE_SUSPENDED : 0);

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

	bool SaveFixtureSecurity(const std::filesystem::path &root, const wchar_t *name)
	{
		std::ofstream out(root / name, std::ios::binary | std::ios::trunc);
		constexpr SECURITY_INFORMATION information =
			DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION;
		for (const auto *relative :
			 { L"", L"bin", L"sources", L"secrets", L"bin/zis_probe.exe",
			   L"sources/main.cpp", L"secrets/token.txt" }) {
			const auto path = root / relative;
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			const DWORD error =
				::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, information,
										nullptr, nullptr, nullptr, nullptr, &descriptor);
			if (error != ERROR_SUCCESS) {
				PrintWin32Error(L"GetNamedSecurityInfoW(DACL/MIC)", error);
				return false;
			}
			const auto freeDescriptor = wil::scope_exit([&] { ::LocalFree(descriptor); });
			LPWSTR sddl = nullptr;
			if (!::ConvertSecurityDescriptorToStringSecurityDescriptorW(
					descriptor, SDDL_REVISION_1, information, &sddl, nullptr)) {
				PrintWin32Error(L"ConvertSecurityDescriptorToStringSecurityDescriptorW");
				return false;
			}
			out << WideToUtf8(path.wstring()) << "\n" << WideToUtf8(sddl) << "\n";
			::LocalFree(sddl);
		}
		out.close();
		return static_cast<bool>(out);
	}

	int RunNativeFileTest(LaunchMode mode, const std::filesystem::path &root,
						  const std::filesystem::path &image, FileTestOptions &options)
	{
		std::setlocale(LC_CTYPE, ".UTF8");
		const bool psec = mode == LaunchMode::Psec;
		const auto pathUtf8 = [&](const wchar_t *name) {
			return WideToUtf8((root / name).wstring());
		};
		options.specification =
			options.noFilesystemPolicy
				? std::vector<std::uint8_t>(kMinimalSandboxSpec.begin(),
											kMinimalSandboxSpec.end())
				: BuildFilePolicy(psec, pathUtf8(L"bin"), pathUtf8(L"sources"),
								  pathUtf8(L"secrets"), options.allowSecrets,
								  options.denyOverridesGrant);
		const auto specPath = root / (psec ? L"psec-files.bin" : L"cpis-files.bin");
		std::ofstream spec(specPath, std::ios::binary | std::ios::trunc);
		spec.write(reinterpret_cast<const char *>(options.specification.data()),
				   static_cast<std::streamsize>(options.specification.size()));
		spec.close();
		if (!spec) {
			std::wcerr << L"Cannot save the actual API specification: "
					   << specPath.wstring() << L"\n";
			return 1;
		}
		std::wcout << L"Backend requested: "
				   << (psec ? L"PSEC 1.1" : L"CPIS / SBOX 0.1.0")
				   << L"\nBinary specification: " << specPath.wstring() << L"\n";
		if (options.noFilesystemPolicy) {
			std::wcout << L"Filesystem fields: absent (diagnostic control); other launch "
						  L"parameters unchanged.\n";
		} else {
			std::wcout << L"bin=read/execute; sources=read/write; secrets="
					   << (options.allowSecrets ? L"read/write (positive control)"
						   : options.denyOverridesGrant
							   ? L"read/write + fs_deny (causal control)"
							   : L"fs_deny")
					   << L"\n";
		}
		std::wcout
			<< L"No filesystem ACL grants or fallback are performed by this launcher.\n";
		if (options.specOnly) {
			std::wcout << L"Specification only: API, policy acceptance and process NOT "
						  L"tested.\n";
			return 0;
		}
		// Confirm both canaries outside the sandbox before attempting any policy.
		for (const auto *relative : { L"sources/main.cpp", L"secrets/token.txt" }) {
			std::ifstream canary(root / relative, std::ios::binary);
			if (!canary || canary.peek() == std::ifstream::traits_type::eof()) {
				std::wcerr << L"Outside-sandbox canary check failed: " << relative
						   << L"\n";
				return 1;
			}
		}
		std::wcout << L"Outside-sandbox canaries: present and readable.\n";
		if (!SaveFixtureSecurity(root, L"security-before.txt"))
			return 1;
		std::wstring userSid, appSidString;
		if (!GetCurrentUserSidString(userSid))
			return 1;
		if (!psec) {
			PSID sid = nullptr;
			const HRESULT hr =
				::DeriveAppContainerSidFromAppContainerName(kCpisFileProfile, &sid);
			if (FAILED(hr)) {
				PrintHresult(L"Derive CPIS file-test SID", hr);
				return 1;
			}
			const bool ok = SidToString(sid, appSidString);
			::FreeSid(sid);
			if (!ok)
				return 1;
			std::wcout << L"CPIS identity: " << kCpisFileProfile << L"\nDerived SID: "
					   << appSidString << L"\n";
		}
		HDESK original = nullptr;
		if (!options.noUi) {
			original = ::OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
			if (!original) {
				PrintWin32Error(L"OpenInputDesktop");
				return 1;
			}
		}
		const auto closeOriginal = wil::scope_exit([&] {
			if (original)
				::CloseDesktop(original);
		});
		HDESK desktop = CreateLowIntegrityDesktop(userSid, appSidString, psec);
		if (!desktop)
			return 1;
		const auto closeDesktop = wil::scope_exit([&] { ::CloseDesktop(desktop); });
		PsecLaunchContext context{};
		const auto closeContext = wil::scope_exit([&] { ClosePsecContext(context); });
		PROCESS_INFORMATION pi{};
		const bool launched =
			psec ? LaunchPsecOnDesktop(image, pi, context, root, &options)
				 : LaunchExperimentalSandboxOnDesktop(image, pi, root, &options);
		if (!launched) {
			SaveFixtureSecurity(root, L"security-after.txt");
			std::wcerr << L"Process launched: no. File policy enforcement: unverified. "
						  L"No fallback.\n";
			return 1;
		}
		const auto closeProcess = wil::scope_exit([&] {
			::CloseHandle(pi.hThread);
			::CloseHandle(pi.hProcess);
		});
		std::wcout << L"Process launched: yes; backend=" << (psec ? L"PSEC" : L"CPIS")
				   << L"; PID=" << pi.dwProcessId
				   << L". Enforcement requires CSV + controls.\n";
		// Both native APIs return a suspended child in file-test mode. Observe
		// host DACL/MIC while the policy is bound, before the probe touches files.
		const bool captured = SaveFixtureSecurity(root, L"security-bound.txt");
		const bool resumed =
			captured && ::ResumeThread(pi.hThread) != static_cast<DWORD>(-1);
		if (!resumed) {
			if (captured)
				PrintWin32Error(L"ResumeThread(file test)");
			::TerminateProcess(pi.hProcess, ERROR_PROCESS_ABORTED);
			::WaitForSingleObject(pi.hProcess, INFINITE);
			return 1;
		}
		const bool switched = options.noUi || ::SwitchDesktop(desktop) != FALSE;
		if (!switched)
			PrintWin32Error(L"SwitchDesktop(file test)");
		const DWORD wait = ::WaitForSingleObject(
			pi.hProcess, switched ? (options.noUi ? 30'000 : 300'000) : 0);
		if (wait == WAIT_FAILED)
			PrintWin32Error(L"WaitForSingleObject(file test)");
		const bool restored = !original || ::SwitchDesktop(original) != FALSE;
		if (!restored)
			PrintWin32Error(L"SwitchDesktop(original)");
		if (wait != WAIT_OBJECT_0) {
			std::wcerr << L"Probe did not finish in time; terminating before releasing "
						  L"PSEC objects/buffers.\n";
			if (!::TerminateProcess(pi.hProcess, ERROR_TIMEOUT))
				PrintWin32Error(L"TerminateProcess(file test)");
			// Never release policy state while a live child may still depend on it.
			if (::WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
				PrintWin32Error(L"WaitForSingleObject(terminated file test)");
				::ExitProcess(1);
			}
			return 1;
		}
		DWORD exitCode = 0;
		if (!::GetExitCodeProcess(pi.hProcess, &exitCode)) {
			PrintWin32Error(L"GetExitCodeProcess");
			return 1;
		}
		std::wcout << L"Probe exit code: " << exitCode << L" / 0x" << std::hex << exitCode
				   << std::dec << L"\n";
		const bool saved = SaveFixtureSecurity(root, L"security-after.txt");
		return switched && restored && saved ? static_cast<int>(exitCode) : 1;
	}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	LaunchMode mode = LaunchMode::Classic;
	PsecLaunchContext psecContext{};
	std::filesystem::path fileTestRoot;
	FileTestOptions fileOptions;

	for (int i = 1; i < argc; ++i) {
		if (::wcscmp(argv[i], L"--classic") == 0) {
			mode = LaunchMode::Classic;
		} else if (::wcscmp(argv[i], L"--experimental") == 0) {
			mode = LaunchMode::Experimental;
		} else if (::wcscmp(argv[i], L"--psec-probe") == 0) {
			mode = LaunchMode::PsecProbe;
		} else if (::wcscmp(argv[i], L"--psec") == 0) {
			mode = LaunchMode::Psec;
		} else if (::wcscmp(argv[i], L"--file-test-allow-secrets") == 0) {
			fileOptions.allowSecrets = true;
		} else if (::wcscmp(argv[i], L"--file-test-deny-overrides-grant") == 0) {
			fileOptions.denyOverridesGrant = true;
		} else if (::wcscmp(argv[i], L"--file-test-no-ui") == 0) {
			fileOptions.noUi = true;
		} else if (::wcscmp(argv[i], L"--file-test-spec-only") == 0) {
			fileOptions.specOnly = true;
		} else if (::wcscmp(argv[i], L"--no-filesystem-policy") == 0) {
			fileOptions.noFilesystemPolicy = true;
		} else if (::wcscmp(argv[i], L"--file-test") == 0) {
			if (!fileTestRoot.empty() || i + 1 == argc || !argv[i + 1][0] ||
				::wcsncmp(argv[i + 1], L"--", 2) == 0) {
				std::wcerr << L"[!] --file-test requires one fixture ROOT.\n";
				return 1;
			}
			fileTestRoot = argv[++i];
		} else if (::wcscmp(argv[i], L"--cleanup") == 0) {
			return Cleanup();
		} else {
			std::wcerr
				<< L"Unknown option: " << argv[i] << L"\n\n"
				<< L"Usage:\n"
				<< L"  check_app_container.exe --classic\n"
				<< L"  check_app_container.exe --classic --file-test ROOT\n"
				<< L"  check_app_container.exe --experimental\n"
				<< L"  check_app_container.exe --experimental --file-test ROOT\n"
				<< L"  check_app_container.exe --cleanup\n"
				<< L"  check_app_container.exe --psec-probe\n"
				<< L"  check_app_container.exe --psec [--file-test ROOT]\n"
				<< L"Native file controls: --file-test-allow-secrets | "
				   L"--file-test-deny-overrides-grant\n"
				<< L"Native file options: --file-test-no-ui, --file-test-spec-only\n"
				<< L"Diagnostic control: --no-filesystem-policy (CPIS file / PSEC GUI)\n";

			return 1;
		}
	}

	std::filesystem::path probePath;
	const bool nativeFileMode =
		mode == LaunchMode::Experimental || mode == LaunchMode::Psec;
	if (fileOptions.noFilesystemPolicy &&
		!((mode == LaunchMode::Experimental && !fileTestRoot.empty()) ||
		  (mode == LaunchMode::Psec && fileTestRoot.empty()))) {
		std::wcerr << L"--no-filesystem-policy requires CPIS --file-test ROOT or PSEC "
					  L"GUI mode.\n";
		return 1;
	}
	if ((fileOptions.allowSecrets || fileOptions.denyOverridesGrant || fileOptions.noUi ||
		 fileOptions.specOnly) &&
		(fileTestRoot.empty() || !nativeFileMode ||
		 (fileOptions.allowSecrets && fileOptions.denyOverridesGrant))) {
		std::wcerr << L"Native file options require --experimental/--psec --file-test "
					  L"ROOT; controls are mutually exclusive.\n";
		return 1;
	}
	if (!fileTestRoot.empty()) {
		if (mode == LaunchMode::PsecProbe) {
			std::wcerr << L"[!] --psec-probe cannot be combined with --file-test.\n";
			return 1;
		}
		std::error_code ec;
		fileTestRoot = std::filesystem::canonical(fileTestRoot, ec);
		if (ec || !std::filesystem::is_directory(fileTestRoot, ec)) {
			std::wcerr << L"[!] File test ROOT must be an existing directory.\n";
			return 1;
		}
		probePath = fileTestRoot / L"bin" / L"zis_probe.exe";
		if (!std::filesystem::is_regular_file(probePath, ec)) {
			std::wcerr << L"[!] File probe not found: " << probePath.wstring() << L"\n";
			return 1;
		}
		if (nativeFileMode)
			return RunNativeFileTest(mode, fileTestRoot, probePath, fileOptions);
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

	if (!fileTestRoot.empty()) {
		std::wcout << L"[+] Classic file probe: " << probePath.wstring() << L"\n";
	} else if (mode == LaunchMode::Psec) {

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

		launched = LaunchAppContainerOnDesktop(appSid, probePath, pi, fileTestRoot);
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

		launched = LaunchPsecOnDesktop(probePath, pi, psecContext, {}, nullptr,
									   fileOptions.noFilesystemPolicy);

		break;
	}

	if (!launched) {
		std::wcerr << L"[!] Process launch failed.\n";

		CloseDesktop(sandboxDesktop);
		CloseDesktop(originalDesktop);
		FreeSid(appSid);

		return 1;
	}

	const DWORD timeoutMs = fileTestRoot.empty() ? 30'000 : 300'000;
	std::wcout << L"[+] Child PID: " << pi.dwProcessId << L"\n"
			   << L"[+] Switching to the ZIS desktop for up to " << timeoutMs / 1000
			   << L" seconds...\n";

	const bool switched = ::SwitchDesktop(sandboxDesktop) != FALSE;
	if (!switched) {
		PrintWin32Error(L"SwitchDesktop(ZIS)");
		if (!fileTestRoot.empty() && !::TerminateProcess(pi.hProcess, 0xDEAD))
			PrintWin32Error(L"TerminateProcess(file test after switch failure)");
	}

	const DWORD wait = ::WaitForSingleObject(pi.hProcess, timeoutMs);
	const DWORD waitError = wait == WAIT_FAILED ? ::GetLastError() : ERROR_SUCCESS;

	// Always return the user to the original input desktop.
	const bool restored = ::SwitchDesktop(originalDesktop) != FALSE;
	if (!restored)
		PrintWin32Error(L"SwitchDesktop(original)");

	if (wait == WAIT_TIMEOUT) {
		std::wcerr << L"[!] Probe timed out after " << timeoutMs / 1000
				   << L" seconds; terminating it.\n";

		::TerminateProcess(pi.hProcess, 0xDEAD);

		// Wait while hProcess is still valid.
		::WaitForSingleObject(pi.hProcess, 5000);
	} else if (wait == WAIT_FAILED) {
		PrintWin32Error(L"WaitForSingleObject", waitError);

		::TerminateProcess(pi.hProcess, 0xDEAD);
		::WaitForSingleObject(pi.hProcess, 5000);
	}

	DWORD exitCode = 0;

	const bool haveExitCode = ::GetExitCodeProcess(pi.hProcess, &exitCode) != FALSE;
	if (haveExitCode) {
		std::wcout << L"[+] Probe exit code: " << exitCode << L" / 0x" << std::hex
				   << exitCode << std::dec << L"\n";
	} else if (!fileTestRoot.empty()) {
		PrintWin32Error(L"GetExitCodeProcess(file test)");
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

	if (!switched || !restored || wait != WAIT_OBJECT_0 || !haveExitCode)
		return 1;

	return static_cast<int>(exitCode);
}
