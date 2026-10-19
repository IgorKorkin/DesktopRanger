#pragma once

#include <algorithm>
#include <cctype>
#include <expected>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <mutex>

#include "internal/window_station_policy_internal.h"

#include <wil/resource.h>
#include <wil/result.h>

#include <Aclapi.h>
#include <Sddl.h>
#include <windows.h>
#pragma comment(lib, "advapi32.lib")

#pragma comment(lib, "Advapi32.lib")

namespace WinstationPatcher
{
	[[nodiscard]] bool IsRunningAsSystemNtAuthority() noexcept
	{
		wil::unique_handle token;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.addressof())) {
			return false;
		}

		DWORD userSize{};
		::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &userSize);

		std::vector<BYTE> userBuffer(userSize);
		PTOKEN_USER tokenUser = reinterpret_cast<PTOKEN_USER>(userBuffer.data());

		if (!::GetTokenInformation(token.get(), TokenUser, tokenUser, userSize,
								   &userSize)) {
			return false;
		}

		::SID_NAME_USE sidType{};
		DWORD userNameSize{ 256 };
		std::string userName(userNameSize, '\0');
		DWORD domainNameSize{ 256 };
		std::string domainName(domainNameSize, '\0');

		if (!::LookupAccountSidA(nullptr, tokenUser->User.Sid, userName.data(),
								 &userNameSize, domainName.data(), &domainNameSize,
								 &sidType)) {
			return false;
		}

		return _stricmp(userName.c_str(), "SYSTEM") == 0 &&
			   _stricmp(domainName.c_str(), "NT AUTHORITY") == 0;
	}

	[[nodiscard]]
	std::expected<bool, DWORD> IsProcessElevated() noexcept
	{
		wil::unique_handle token;

		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.addressof())) {
			return std::unexpected(::GetLastError());
		}

		::TOKEN_ELEVATION elevation{};
		DWORD size = sizeof(elevation);

		if (!::GetTokenInformation(token.get(), TOKEN_INFORMATION_CLASS::TokenElevation,
								   &elevation, sizeof(elevation), &size)) {
			return std::unexpected(::GetLastError());
		}

		return elevation.TokenIsElevated != 0;
	}

	// RAII для HWINSTA
	using unique_hwinsta =
		wil::unique_any<::HWINSTA, decltype(&::CloseWindowStation), ::CloseWindowStation>;

	enum class enum_desktop_mode { grant, revoke };

	// ------------------------------------------------------------

	[[nodiscard]]
	std::wstring get_error_message(DWORD error) noexcept
	{
		wil::unique_hlocal_string buffer;

		const DWORD size = ::FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

		if (size == 0)
			return L"Unknown error";

		return std::wstring(buffer.get(), size);
	}

	void PrintAccessRights(DWORD mask)
	{
		std::cout << "    Rights: 0x" << std::hex << mask << std::dec << std::endl;

		auto has = [mask](DWORD right) { return (mask & right) == right; };

		if (has(WINSTA_ALL_ACCESS))
			std::cout << "    - WINSTA_ALL_ACCESS\n";
		if (has(WINSTA_ENUMDESKTOPS))
			std::cout << "    - WINSTA_ENUMDESKTOPS <<<<<<<<<<<<<<<<<<<<<<\n";
		if (has(WINSTA_ENUMERATE))
			std::cout << "    - WINSTA_ENUMERATE\n";
		if (has(WINSTA_CREATEDESKTOP))
			std::cout << "    - WINSTA_CREATEDESKTOP\n";
		if (has(WINSTA_READATTRIBUTES))
			std::cout << "    - WINSTA_READATTRIBUTES\n";
		if (has(WINSTA_ACCESSCLIPBOARD))
			std::cout << "    - WINSTA_ACCESSCLIPBOARD\n";
		if (has(WINSTA_ACCESSGLOBALATOMS))
			std::cout << "    - WINSTA_ACCESSGLOBALATOMS\n";
		if (has(WINSTA_EXITWINDOWS))
			std::cout << "    - WINSTA_EXITWINDOWS\n";
		if (has(WINSTA_READSCREEN))
			std::cout << "    - WINSTA_READSCREEN\n";
		if (has(WINSTA_WRITEATTRIBUTES))
			std::cout << "    - WINSTA_WRITEATTRIBUTES\n";
	}

	const char *GetNameSidType(SID_NAME_USE sidType)
	{
		switch (sidType) {
		case SidTypeUser:
			return "User";
		case SidTypeGroup:
			return "Group";
		case SidTypeDomain:
			return "Domain";
		case SidTypeAlias:
			return "Alias";
		case SidTypeWellKnownGroup:
			return "Well-known group";
		case SidTypeDeletedAccount:
			return "Deleted";
		case SidTypeInvalid:
			return "Invalid";
		case SidTypeUnknown:
			return "Unknown";
		case SidTypeComputer:
			return "Computer";
		case SidTypeLabel:
			return "Label";
		case SidTypeLogonSession:
			return "LogonSession";
		default:
			return "Unknown Type";
		}
	}

	void PrintSidInfo(PSID pSid)
	{
		char name[256], domain[256];
		DWORD nameLen = sizeof(name);
		DWORD domainLen = sizeof(domain);
		SID_NAME_USE sidType{};

		do {
			if (!::LookupAccountSidA(nullptr, pSid, name, &nameLen, domain, &domainLen,
									 &sidType)) {
				std::cout << "  [!] LookupAccountSid failed: " << GetLastError()
						  << std::endl;
				break;
			}
			std::cout << "  Account: " << domain << "\\" << name << std::endl;
			std::cout << "  Type: " << GetNameSidType(sidType) << std::endl;

		} while (false);

		char *sidStr = nullptr;
		if (::ConvertSidToStringSidA(pSid, &sidStr)) {
			std::cout << "  SID: " << sidStr << std::endl;
			::LocalFree(sidStr);
		}
	}

	void DumpAcl(PACL acl)
	{
		if (!acl) {
			std::cout << "  [!] NULL ACL\n";
			return;
		}

		::ACL_SIZE_INFORMATION aclInfo{};
		if (!::GetAclInformation(acl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
			std::cout << "  [!] GetAclInformation failed: " << ::GetLastError() << "\n";
			return;
		}

		std::cout << "  ACE count: " << aclInfo.AceCount << "\n\n";

		for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
			LPVOID pAce = nullptr;

			if (!::GetAce(acl, i, &pAce)) {
				std::cout << "  [!] GetAce failed: " << ::GetLastError() << "\n";
				continue;
			}

			auto header = static_cast<PACE_HEADER>(pAce);

			std::cout << "ACE #" << i << "\n";

			// --- Тип ACE ---
			switch (header->AceType) {
			case ACCESS_ALLOWED_ACE_TYPE:
				std::cout << "  Type: ACCESS_ALLOWED\n";
				break;

			case ACCESS_DENIED_ACE_TYPE:
				std::cout << "  Type: ACCESS_DENIED\n";
				break;

			default:
				std::cout << "  Type: OTHER (" << static_cast<int>(header->AceType)
						  << ")\n";
				break;
			}

			if (header->AceFlags & INHERITED_ACE)
				std::cout << "  Flags: INHERITED\n";

			if (header->AceFlags & OBJECT_INHERIT_ACE)
				std::cout << "  Flags: OBJECT_INHERIT\n";

			if (header->AceFlags & CONTAINER_INHERIT_ACE)
				std::cout << "  Flags: CONTAINER_INHERIT\n";

			// --- ACCESS_ALLOWED ---
			if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
				auto allowed = reinterpret_cast<PACCESS_ALLOWED_ACE>(pAce);

				PrintAccessRights(allowed->Mask);

				PSID sid = &allowed->SidStart;
				PrintSidInfo(sid);
			} else if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
				auto denied = reinterpret_cast<PACCESS_DENIED_ACE>(pAce);

				PrintAccessRights(denied->Mask);

				PSID sid = &denied->SidStart;
				PrintSidInfo(sid);
			}

			std::cout << "--------------------------------------\n";
		}
	}

	void DumpWindowStationDacl(HWINSTA hWinSta)
	{
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR sd = nullptr;

		DWORD status =
			::GetSecurityInfo(hWinSta, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
							  nullptr, nullptr, &dacl, nullptr, &sd);

		if (status != ERROR_SUCCESS) {
			std::cout << "[!] GetSecurityInfo failed: " << status << "\n";
			return;
		}

		std::unique_ptr<void, decltype(&::LocalFree)> sdHolder(sd, ::LocalFree);

		std::cout << "\n===== DACL DUMP =====\n\n";

		DumpAcl(dacl);
	}
	// ------------------------------------------------------------

	// Compatibility entry points retain their paired revoke/grant lifecycle.
	// Each station owns its original snapshot and a live handle until restoration.
	namespace Policy = DesktopRanger::WindowStationPolicy;
	struct LegacySession {
		Policy::UniqueHandle station;
		Policy::RestrictedSession restriction;

		explicit LegacySession(Policy::UniqueHandle handle)
			: station(std::move(handle)), restriction(station.get(), [](DWORD) noexcept {
				::OutputDebugStringW(L"DesktopRanger: legacy Window Station restore failed.\n");
			}) {}
	};

	std::expected<void, DWORD> ProcessWindowStation(std::wstring_view name,
		enum_desktop_mode mode) noexcept try
	{
		static std::mutex mutex;
		static std::map<std::wstring, std::unique_ptr<LegacySession>> sessions;
		std::lock_guard lock(mutex);
		const std::wstring key(name);
		auto position = sessions.find(key);
		if (mode == enum_desktop_mode::grant) {
			if (position == sessions.end()) {
				return std::unexpected(ERROR_NOT_FOUND);
			}
			auto result = position->second->restriction.Restore();
			if (result) {
				sessions.erase(position);
			}
			return result;
		}
		if (mode != enum_desktop_mode::revoke) {
			return std::unexpected(ERROR_INVALID_PARAMETER);
		}
		if (position != sessions.end()) {
			return std::unexpected(ERROR_ALREADY_EXISTS);
		}
		auto station = Policy::OpenStation(name);
		if (!station) {
			return std::unexpected(station.error());
		}
		auto session = std::make_unique<LegacySession>(std::move(station.value()));
		// Allocate the registry entry before changing any ACL.
		auto entry = sessions.emplace(key, std::move(session)).first;
		auto result = entry->second->restriction.Restrict();
		if (!result) {
			sessions.erase(entry);
		}
		return result;
	}
	catch (const std::bad_alloc &) {
		return std::unexpected(ERROR_NOT_ENOUGH_MEMORY);
	}
	catch (...) {
		return std::unexpected(ERROR_GEN_FAILURE);
	}

	void ProcessAllExistingStations(enum_desktop_mode mode)
	{
		::EnumWindowStationsW(
			[](LPWSTR name, LPARAM param) -> BOOL {
				const auto mode = static_cast<enum_desktop_mode>(param);

				const auto result = ProcessWindowStation(name, mode);

				if (!result) {
					const DWORD err = result.error();

					std::wcerr << L"[!] " << name << L" failed. Error " << err << L": "
							   << get_error_message(err) << L"\n";
				} else {
					std::wcout << L"[+] " << name << L" updated successfully\n";
				}

				return TRUE;
			},
			static_cast<LPARAM>(mode));
	}

	void ProcessOnlyWinsta0(enum_desktop_mode mode)
	{
		const auto name = L"WinSta0";
		const auto result = ProcessWindowStation(name, mode);

		if (!result) {
			const DWORD err = result.error();

			std::wcerr << L"[!] " << name << L" failed. Error " << err << L": "
					   << get_error_message(err) << L"\n";
		} else {
			std::wcout << L"[+] " << name << L" updated successfully\n";
		}
	}

	void RevokeWinsta()
	{
		return ProcessOnlyWinsta0(enum_desktop_mode::revoke);
	}

	void GrantWinsta()
	{
		return ProcessOnlyWinsta0(enum_desktop_mode::grant);
	}

} // namespace WinstationPatcher
