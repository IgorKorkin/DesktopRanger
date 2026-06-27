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

	static std::expected<void, DWORD>
	ProcessEnumDesktopMask(HWINSTA hWinSta, enum_desktop_mode mode) noexcept
	{
		PACL oldDacl = nullptr;
		PSECURITY_DESCRIPTOR sd = nullptr;

		const DWORD status =
			::GetSecurityInfo(hWinSta, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
							  nullptr, nullptr, &oldDacl, nullptr, &sd);

		if (status != ERROR_SUCCESS)
			return std::unexpected(status);

		wil::unique_any<PSECURITY_DESCRIPTOR, decltype(&::LocalFree), ::LocalFree>
			sdHolder(sd);

		if (!oldDacl)
			return std::unexpected(ERROR_INVALID_ACL);

		std::print("\n=== BEFORE ===\n");
		DumpAcl(oldDacl);

		::ACL_SIZE_INFORMATION aclInfo{};
		if (!::GetAclInformation(oldDacl, &aclInfo, sizeof(aclInfo),
								 AclSizeInformation)) {
			return std::unexpected(::GetLastError());
		}

		// Создаём новый ACL такого же размера
		const DWORD newAclSize = aclInfo.AclBytesInUse;

		auto newAcl = static_cast<PACL>(::LocalAlloc(LPTR, newAclSize));

		if (!newAcl)
			return std::unexpected(::GetLastError());

		wil::unique_any<PACL, decltype(&::LocalFree), ::LocalFree> newAclHolder(newAcl);

		if (!::InitializeAcl(newAcl, newAclSize, ACL_REVISION)) {
			return std::unexpected(::GetLastError());
		}

		// Копируем ACE по порядку
		for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
			LPVOID pAce = nullptr;

			if (!::GetAce(oldDacl, i, &pAce))
				return std::unexpected(::GetLastError());

			const auto header = static_cast<PACE_HEADER>(pAce);

			// Копируем ACE в буфер
			std::vector<std::byte> aceCopy(header->AceSize);

			std::memcpy(aceCopy.data(), pAce, header->AceSize);

			// Меняем маску только если ACCESS_ALLOWED
			if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
				auto allowed = reinterpret_cast<PACCESS_ALLOWED_ACE>(aceCopy.data());

				if (mode == enum_desktop_mode::grant) {
					if (!(allowed->Mask & WINSTA_ENUMDESKTOPS)) {
						allowed->Mask |= WINSTA_ENUMDESKTOPS;
					}

				} else if (mode == enum_desktop_mode::revoke) {
					if (allowed->Mask & WINSTA_ENUMDESKTOPS) {
						allowed->Mask &= ~WINSTA_ENUMDESKTOPS;
					}
				} else {
					std::cout << "Unknown enum_desktop_mode \n";
				}
			}

			if (!::AddAce(newAcl, ACL_REVISION, MAXDWORD, aceCopy.data(),
						  header->AceSize)) {
				return std::unexpected(::GetLastError());
			}
		}

		std::print("\n=== AFTER ===\n");
		DumpAcl(newAcl);

		const DWORD applyStatus =
			::SetSecurityInfo(hWinSta, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
							  nullptr, nullptr, newAcl, nullptr);

		if (applyStatus != ERROR_SUCCESS)
			return std::unexpected(applyStatus);

		return {};
	}

	// ------------------------------------------------------------

	[[nodiscard]]
	std::expected<void, DWORD> ProcessWindowStation(std::wstring_view name,
													enum_desktop_mode mode) noexcept
	{
		unique_hwinsta hWinSta{ ::OpenWindowStationW(name.data(), FALSE,
													 READ_CONTROL | WRITE_DAC) };

		if (!hWinSta)
			return std::unexpected(::GetLastError());

		return ProcessEnumDesktopMask(hWinSta.get(), mode);
	}

	// ------------------------------------------------------------

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