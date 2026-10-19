#include "pch.h"

#include <Windows.h>

#include <Sddl.h>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "internal/secure_application_launcher_internal.h"

namespace DesktopRanger::Tests
{
	namespace Launcher = SecureApplicationLauncher;
	namespace Internal = Launcher::detail;
	namespace Policy = WindowStationPolicy;

	class SecureApplicationLauncher : public ::testing::Test {
	protected:
		inline static thread_local SecureApplicationLauncher *active{};
		Policy::UniqueSecurityDescriptor desktop, station;
		wil::unique_hlocal sid;
		std::vector<std::string> events;
		std::vector<HANDLE> closed;
		std::map<int, DWORD> desktopFailures, stationFailures;
		int desktopWrites{}, stationWrites{}, creates{}, callbacks{}, tokenReads{};
		std::optional<DWORD> tokenOpenError, tokenProbeError, tokenReadError,
			stationOpenError, desktopReadError, stationReadError, createError;
		bool defaultDesktop{}, missingThread{};

		static HDESK Desktop()
		{
			return reinterpret_cast<HDESK>(1);
		}
		static HWINSTA Station()
		{
			return reinterpret_cast<HWINSTA>(2);
		}
		static HANDLE Token()
		{
			return reinterpret_cast<HANDLE>(3);
		}
		static HANDLE Process()
		{
			return reinterpret_cast<HANDLE>(4);
		}
		static HANDLE Thread()
		{
			return reinterpret_cast<HANDLE>(5);
		}

		static Policy::UniqueSecurityDescriptor Descriptor(const wchar_t *sddl)
		{
			PSECURITY_DESCRIPTOR result{};
			EXPECT_TRUE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
				sddl, SDDL_REVISION_1, &result, nullptr));
			return Policy::UniqueSecurityDescriptor(result);
		}
		void SetUp() override
		{
			active = this;
			desktop = Descriptor(L"D:P(D;;0x1;;;WD)(A;;GA;;;SY)(A;;0x2;;;BA)");
			station = Descriptor(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
			PSID raw{};
			ASSERT_TRUE(::ConvertStringSidToSidW(L"S-1-5-21-1-2-3-1001", &raw));
			sid.reset(raw);
		}
		void TearDown() override
		{
			active = nullptr;
		}
		static std::wstring Sddl(PSECURITY_DESCRIPTOR descriptor)
		{
			wchar_t *raw{};
			EXPECT_TRUE(::ConvertSecurityDescriptorToStringSecurityDescriptorW(
				descriptor, SDDL_REVISION_1, DACL_SECURITY_INFORMATION, &raw, nullptr));
			wil::unique_hlocal_string owner(raw);
			return raw ? raw : L"";
		}
		static BOOL WINAPI OpenToken(HANDLE process, DWORD access, PHANDLE token)
		{
			EXPECT_EQ(process, ::GetCurrentProcess());
			EXPECT_EQ(access, TOKEN_QUERY);
			if (active->tokenOpenError) {
				::SetLastError(*active->tokenOpenError);
				return FALSE;
			}
			*token = Token();
			return TRUE;
		}
		static BOOL WINAPI TokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind,
											LPVOID buffer, DWORD size, PDWORD required)
		{
			EXPECT_EQ(token, Token());
			EXPECT_EQ(kind, TokenUser);
			++active->tokenReads;
			*required = sizeof(TOKEN_USER);
			if (!buffer) {
				::SetLastError(
					active->tokenProbeError.value_or(ERROR_INSUFFICIENT_BUFFER));
				return FALSE;
			}
			if (active->tokenReadError) {
				::SetLastError(*active->tokenReadError);
				return FALSE;
			}
			EXPECT_GE(size, sizeof(TOKEN_USER));
			static_cast<TOKEN_USER *>(buffer)->User.Sid = active->sid.get();
			return TRUE;
		}
		static HDESK WINAPI CurrentDesktop(DWORD)
		{
			return active->defaultDesktop ? Desktop() : reinterpret_cast<HDESK>(9);
		}
		static HWINSTA WINAPI OpenStation(LPCWSTR name, BOOL inherit, ACCESS_MASK access)
		{
			active->events.emplace_back("open station");
			EXPECT_STREQ(name, L"WinSta0");
			EXPECT_FALSE(inherit);
			EXPECT_EQ(access, READ_CONTROL | WRITE_DAC);
			if (active->stationOpenError) {
				::SetLastError(*active->stationOpenError);
				return nullptr;
			}
			return Station();
		}
		static BOOL WINAPI CloseStation(HWINSTA station)
		{
			EXPECT_EQ(station, Station());
			active->events.emplace_back("close station");
			return TRUE;
		}
		static BOOL WINAPI Close(HANDLE handle)
		{
			EXPECT_TRUE(handle == Token() || handle == Process() || handle == Thread());
			active->closed.push_back(handle);
			active->events.emplace_back(handle == Token() ? "close token"
														  : "close process handle");
			return TRUE;
		}
		static DWORD WINAPI Read(HANDLE object, SE_OBJECT_TYPE type,
								 SECURITY_INFORMATION info, PSID *, PSID *, PACL *,
								 PACL *, PSECURITY_DESCRIPTOR *result)
		{
			EXPECT_EQ(type, SE_WINDOW_OBJECT);
			EXPECT_EQ(info, DACL_SECURITY_INFORMATION);
			const bool isDesktop = object == Desktop();
			EXPECT_TRUE(isDesktop || object == Station());
			active->events.emplace_back(isDesktop ? "snapshot desktop"
												  : "snapshot station");
			const auto failure =
				isDesktop ? active->desktopReadError : active->stationReadError;
			if (failure)
				return *failure;
			const auto source = isDesktop ? active->desktop.get() : active->station.get();
			const auto size = ::GetSecurityDescriptorLength(source);
			*result = ::LocalAlloc(LPTR, size);
			if (!*result)
				return ERROR_NOT_ENOUGH_MEMORY;
			std::memcpy(*result, source, size);
			return ERROR_SUCCESS;
		}
		static DWORD WINAPI Write(HANDLE object, SE_OBJECT_TYPE type,
								  SECURITY_INFORMATION info, PSID, PSID, PACL dacl, PACL)
		{
			EXPECT_EQ(type, SE_WINDOW_OBJECT);
			const bool isDesktop = object == Desktop();
			EXPECT_TRUE(isDesktop || object == Station());
			auto &count = isDesktop ? active->desktopWrites : active->stationWrites;
			++count;
			active->events.emplace_back(
				isDesktop ? (count == 1 ? "allow desktop" : "restore desktop")
						  : (count == 1 ? "restrict station" : "restore station"));
			auto &failures =
				isDesktop ? active->desktopFailures : active->stationFailures;
			if (const auto failure = failures.find(count); failure != failures.end())
				return failure->second;
			auto &current = isDesktop ? active->desktop : active->station;
			SECURITY_DESCRIPTOR descriptor{};
			EXPECT_TRUE(::InitializeSecurityDescriptor(&descriptor,
													   SECURITY_DESCRIPTOR_REVISION));
			EXPECT_TRUE(::SetSecurityDescriptorDacl(&descriptor, TRUE, dacl, FALSE));
			SECURITY_DESCRIPTOR_CONTROL control{};
			DWORD revision{};
			EXPECT_TRUE(
				::GetSecurityDescriptorControl(current.get(), &control, &revision));
			if (info & PROTECTED_DACL_SECURITY_INFORMATION)
				control |= SE_DACL_PROTECTED;
			if (info & UNPROTECTED_DACL_SECURITY_INFORMATION)
				control &= ~SE_DACL_PROTECTED;
			EXPECT_TRUE(::SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED,
													   control & SE_DACL_PROTECTED));
			DWORD size{};
			::MakeSelfRelativeSD(&descriptor, nullptr, &size);
			Policy::UniqueSecurityDescriptor copy(::LocalAlloc(LPTR, size));
			EXPECT_TRUE(::MakeSelfRelativeSD(&descriptor, copy.get(), &size));
			current = std::move(copy);
			return ERROR_SUCCESS;
		}
		static BOOL WINAPI Create(LPCWSTR app, LPWSTR command,
								  LPSECURITY_ATTRIBUTES processSA,
								  LPSECURITY_ATTRIBUTES threadSA, BOOL inherit,
								  DWORD flags, LPVOID environment, LPCWSTR directory,
								  LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process)
		{
			++active->creates;
			active->events.emplace_back("create");
			EXPECT_EQ(app, nullptr);
			EXPECT_STREQ(command, L"example.exe --argument");
			command[0] = L'E'; // Windows may modify its command-line buffer.
			EXPECT_EQ(processSA, nullptr);
			EXPECT_EQ(threadSA, nullptr);
			EXPECT_FALSE(inherit);
			EXPECT_EQ(flags, 0u);
			EXPECT_EQ(environment, nullptr);
			EXPECT_EQ(directory, nullptr);
			EXPECT_EQ(startup->cb, sizeof(STARTUPINFOW));
			EXPECT_TRUE(std::wstring_view(startup->lpDesktop) == L"Secure" ||
						std::wstring_view(startup->lpDesktop) == L"Default");
			EXPECT_EQ(startup->dwFlags, STARTF_USESHOWWINDOW);
			EXPECT_EQ(startup->wShowWindow, SW_SHOW);
			if (active->createError) {
				::SetLastError(*active->createError);
				return FALSE;
			}
			*process = { Process(), active->missingThread ? nullptr : Thread(), 42, 43 };
			return TRUE;
		}
		Internal::LaunchApi Api()
		{
			return {
				&OpenToken,	   &TokenInformation, &CurrentDesktop, &OpenStation,
				&CloseStation, &Create,			  &Close,		   { &Read, &Write }
			};
		}
		std::expected<void, DWORD> Observe(const PROCESS_INFORMATION &process)
		{
			++callbacks;
			events.emplace_back("callback");
			EXPECT_EQ(process.hProcess, Process());
			EXPECT_EQ(process.hThread, Thread());
			EXPECT_EQ(process.dwProcessId, 42u);
			EXPECT_EQ(closed, (std::vector<HANDLE>{ Token() }));
			return {};
		}
		std::expected<DWORD, Launcher::LaunchError>
		Run(std::wstring_view name = L"Secure")
		{
			return Internal::LaunchApplication(
				Desktop(), name, L"example.exe --argument",
				[&](const PROCESS_INFORMATION &p) { return Observe(p); }, Api());
		}
		void ExpectFailure(const std::expected<DWORD, Launcher::LaunchError> &result,
						   Launcher::LaunchStep step, DWORD code)
		{
			ASSERT_FALSE(result);
			ASSERT_TRUE(result.error().failure);
			EXPECT_EQ(result.error().failure->step, step);
			EXPECT_EQ(result.error().failure->error, code);
		}
	};

	TEST_F(SecureApplicationLauncher, SuccessRestoresBothDaclsAndClosesOwnedHandles)
	{
		const auto originalDesktop = Sddl(desktop.get()),
				   originalStation = Sddl(station.get());
		auto result = Run();
		ASSERT_TRUE(result);
		EXPECT_EQ(*result, 42u);
		EXPECT_EQ(Sddl(desktop.get()), originalDesktop);
		EXPECT_EQ(Sddl(station.get()), originalStation);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token(), Thread(), Process() }));
		EXPECT_EQ(events,
				  (std::vector<std::string>{
					  "close token", "open station", "snapshot station",
					  "restrict station", "snapshot desktop", "allow desktop", "create",
					  "callback", "close process handle", "close process handle",
					  "restore desktop", "restore station", "close station" }));
	}
	TEST_F(SecureApplicationLauncher, TemporaryPolicyGrantsOnlyCurrentUserGenericAll)
	{
		auto result = Internal::LaunchApplication(
			Desktop(), L"Secure", L"example.exe --argument",
			[&](const PROCESS_INFORMATION &p) -> std::expected<void, DWORD> {
				EXPECT_NE(Sddl(desktop.get()).find(L"(A;;GA;;;S-1-5-21-1-2-3-1001)"),
						  std::wstring::npos);
				const auto dacl = Policy::GetDacl(desktop.get());
				EXPECT_EQ(dacl.value()->AceCount, 1);
				const auto stationAcl = Policy::GetDacl(station.get());
				const auto ace = Policy::GetAceAt(stationAcl.value(), 0);
				EXPECT_FALSE(
					reinterpret_cast<const ACCESS_ALLOWED_ACE *>(ace.value())->Mask &
					WINSTA_ENUMDESKTOPS);
				return Observe(p);
			},
			Api());
		EXPECT_TRUE(result);
	}
	TEST_F(SecureApplicationLauncher, DefaultDoesNotTouchAnyDaclOrStation)
	{
		defaultDesktop = true;
		EXPECT_TRUE(Run(L"Default"));
		EXPECT_EQ(desktopWrites, 0);
		EXPECT_EQ(stationWrites, 0);
		EXPECT_EQ(events, (std::vector<std::string>{ "close token", "create", "callback",
													 "close process handle",
													 "close process handle" }));
	}
	TEST_F(SecureApplicationLauncher, DefaultNameWithDifferentHandleStillUsesPolicy)
	{
		EXPECT_TRUE(Run(L"Default"));
		EXPECT_EQ(desktopWrites, 2);
	}
	TEST_F(SecureApplicationLauncher, PreservesUnprotectedDaclAndAceOrder)
	{
		desktop = Descriptor(L"D:(D;;0x2;;;WD)(A;;GA;;;BA)(A;;0x4;;;SY)");
		const auto original = Sddl(desktop.get());
		EXPECT_TRUE(Run());
		EXPECT_EQ(Sddl(desktop.get()), original);
	}
	TEST_F(SecureApplicationLauncher, PreservesNullDesktopDacl)
	{
		desktop = Descriptor(L"D:NO_ACCESS_CONTROL");
		EXPECT_TRUE(Run());
		ASSERT_TRUE(Policy::GetDacl(desktop.get()));
		EXPECT_EQ(Policy::GetDacl(desktop.get()).value(), nullptr);
	}
	TEST_F(SecureApplicationLauncher, PreservesEmptyProtectedDesktopDacl)
	{
		desktop = Descriptor(L"D:P");
		const auto original = Sddl(desktop.get());
		EXPECT_TRUE(Run());
		EXPECT_EQ(Sddl(desktop.get()), original);
	}
	TEST_F(SecureApplicationLauncher, InvalidInputMakesNoOsCalls)
	{
		for (const auto name : { L"", L"Secure" }) {
			auto result = Internal::LaunchApplication(
				nullptr, name, L"example.exe",
				[](const PROCESS_INFORMATION &) -> std::expected<void, DWORD> {
					return {};
				},
				Api());
			ExpectFailure(result, Launcher::LaunchStep::Validate,
						  ERROR_INVALID_PARAMETER);
		}
		EXPECT_TRUE(events.empty());
	}
	TEST_F(SecureApplicationLauncher, EmbeddedNullAndMissingCallbackAreRejected)
	{
		const std::wstring embedded(L"Secure\0other", 12);
		ExpectFailure(Run(embedded), Launcher::LaunchStep::Validate,
					  ERROR_INVALID_PARAMETER);
		ExpectFailure(
			Internal::LaunchApplication(Desktop(), L"Secure", L"example.exe", {}, Api()),
			Launcher::LaunchStep::Validate, ERROR_INVALID_PARAMETER);
		EXPECT_TRUE(events.empty());
	}
	TEST_F(SecureApplicationLauncher, TokenOpenFailureStopsBeforePolicy)
	{
		tokenOpenError = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::UserSid, ERROR_ACCESS_DENIED);
		EXPECT_TRUE(closed.empty());
		EXPECT_TRUE(events.empty());
	}
	TEST_F(SecureApplicationLauncher, TokenProbeFailureClosesToken)
	{
		tokenProbeError = ERROR_INVALID_HANDLE;
		ExpectFailure(Run(), Launcher::LaunchStep::UserSid, ERROR_INVALID_HANDLE);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token() }));
	}
	TEST_F(SecureApplicationLauncher, TokenReadFailureClosesToken)
	{
		tokenReadError = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::UserSid, ERROR_ACCESS_DENIED);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token() }));
	}
	TEST_F(SecureApplicationLauncher, TokenFailureWithZeroLastErrorIsFailure)
	{
		tokenOpenError = 0;
		ExpectFailure(Run(), Launcher::LaunchStep::UserSid, 0);
	}
	TEST_F(SecureApplicationLauncher, StationOpenFailureStopsBeforeCreate)
	{
		stationOpenError = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::OpenStation, ERROR_ACCESS_DENIED);
		EXPECT_EQ(creates, 0);
	}
	TEST_F(SecureApplicationLauncher, StationSnapshotFailureClosesStation)
	{
		stationReadError = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::RestrictStation, ERROR_ACCESS_DENIED);
		EXPECT_EQ(stationWrites, 0);
		EXPECT_EQ(events.back(), "close station");
	}
	TEST_F(SecureApplicationLauncher, StationRestrictFailureDoesNotTouchDesktop)
	{
		stationFailures[1] = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::RestrictStation, ERROR_ACCESS_DENIED);
		EXPECT_EQ(desktopWrites, 0);
		EXPECT_EQ(creates, 0);
	}
	TEST_F(SecureApplicationLauncher, DesktopSnapshotFailureRestoresStation)
	{
		const auto original = Sddl(station.get());
		desktopReadError = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::SnapshotDesktop, ERROR_ACCESS_DENIED);
		EXPECT_EQ(Sddl(station.get()), original);
		EXPECT_EQ(desktopWrites, 0);
		EXPECT_EQ(creates, 0);
	}
	TEST_F(SecureApplicationLauncher, DesktopAllowFailureRestoresBoth)
	{
		const auto original = Sddl(desktop.get());
		desktopFailures[1] = ERROR_ACCESS_DENIED;
		ExpectFailure(Run(), Launcher::LaunchStep::AllowDesktop, ERROR_ACCESS_DENIED);
		EXPECT_EQ(Sddl(desktop.get()), original);
		EXPECT_EQ(stationWrites, 2);
		EXPECT_EQ(creates, 0);
	}
	TEST_F(SecureApplicationLauncher,
		   CreateFailureRestoresBothWithoutClosingUnownedHandles)
	{
		const auto original = Sddl(desktop.get());
		createError = ERROR_FILE_NOT_FOUND;
		auto result = Run();
		ExpectFailure(result, Launcher::LaunchStep::CreateProcess, ERROR_FILE_NOT_FOUND);
		EXPECT_EQ(result.error().processId, 0u);
		EXPECT_EQ(callbacks, 0);
		EXPECT_EQ(Sddl(desktop.get()), original);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token() }));
	}
	TEST_F(SecureApplicationLauncher, CreateFailureWithZeroLastErrorIsFailure)
	{
		createError = 0;
		ExpectFailure(Run(), Launcher::LaunchStep::CreateProcess, 0);
	}
	TEST_F(SecureApplicationLauncher,
		   IncompleteProcessInformationStillClosesReturnedHandle)
	{
		missingThread = true;
		auto result = Run();
		ExpectFailure(result, Launcher::LaunchStep::CreateProcess, ERROR_INVALID_HANDLE);
		EXPECT_EQ(result.error().processId, 42u);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token(), Process() }));
	}
	TEST_F(SecureApplicationLauncher, CallbackFailureRetainsPidAndClosesBothHandles)
	{
		auto result = Internal::LaunchApplication(
			Desktop(), L"Secure", L"example.exe --argument",
			[](const PROCESS_INFORMATION &) -> std::expected<void, DWORD> {
				return std::unexpected(0);
			},
			Api());
		ExpectFailure(result, Launcher::LaunchStep::Callback, 0);
		EXPECT_EQ(result.error().processId, 42u);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Token(), Thread(), Process() }));
		EXPECT_EQ(desktopWrites, 2);
		EXPECT_EQ(stationWrites, 2);
	}
	TEST_F(SecureApplicationLauncher, CallbackExceptionRestoresAndClosesResources)
	{
		auto result = Internal::LaunchApplication(
			Desktop(), L"Secure", L"example.exe --argument",
			[](const PROCESS_INFORMATION &) -> std::expected<void, DWORD> {
				throw std::runtime_error("callback");
			},
			Api());
		ExpectFailure(result, Launcher::LaunchStep::Callback, ERROR_UNHANDLED_EXCEPTION);
		EXPECT_EQ(result.error().processId, 42u);
		EXPECT_EQ(closed.size(), 3u);
		EXPECT_EQ(desktopWrites, 2);
		EXPECT_EQ(stationWrites, 2);
	}
	TEST_F(SecureApplicationLauncher,
		   TransientDesktopRestoreFailureRetriesOriginalSnapshot)
	{
		const auto original = Sddl(desktop.get());
		desktopFailures[2] = ERROR_ACCESS_DENIED;
		auto result = Run();
		ASSERT_FALSE(result);
		EXPECT_FALSE(result.error().failure);
		EXPECT_EQ(result.error().processId, 42u);
		EXPECT_EQ(result.error().desktopRestoreError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(desktopWrites, 3);
		EXPECT_EQ(Sddl(desktop.get()), original);
		EXPECT_EQ(events[events.size() - 3], "restore desktop");
		EXPECT_EQ(events[events.size() - 2], "restore station");
	}
	TEST_F(SecureApplicationLauncher, TransientStationRestoreFailureRetriesSnapshot)
	{
		const auto original = Sddl(station.get());
		stationFailures[2] = ERROR_ACCESS_DENIED;
		auto result = Run();
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().stationRestoreError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(stationWrites, 3);
		EXPECT_EQ(Sddl(station.get()), original);
	}
	TEST_F(SecureApplicationLauncher, BothRestoreErrorsDoNotHideCreateFailure)
	{
		createError = ERROR_FILE_NOT_FOUND;
		desktopFailures = { { 2, ERROR_ACCESS_DENIED }, { 3, ERROR_ACCESS_DENIED } };
		stationFailures = { { 2, ERROR_INVALID_HANDLE },
							{ 3, ERROR_INVALID_HANDLE },
							{ 4, ERROR_INVALID_HANDLE } };
		auto result = Run();
		ExpectFailure(result, Launcher::LaunchStep::CreateProcess, ERROR_FILE_NOT_FOUND);
		EXPECT_EQ(result.error().desktopRestoreError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(result.error().stationRestoreError, ERROR_INVALID_HANDLE);
		EXPECT_EQ(events.back(), "close station");
	}
	TEST_F(SecureApplicationLauncher, BothRestoreErrorsRetainSuccessfulProcessPid)
	{
		desktopFailures[2] = ERROR_ACCESS_DENIED;
		stationFailures[2] = ERROR_INVALID_HANDLE;
		auto result = Run();
		ASSERT_FALSE(result);
		EXPECT_FALSE(result.error().failure);
		EXPECT_EQ(result.error().processId, 42u);
		EXPECT_TRUE(result.error().desktopRestoreError);
		EXPECT_TRUE(result.error().stationRestoreError);
		EXPECT_EQ(closed.size(), 3u);
	}
	TEST_F(SecureApplicationLauncher, ReentrantLaunchIsRejectedAndNextLaunchSucceeds)
	{
		auto result = Internal::LaunchApplication(
			Desktop(), L"Secure", L"example.exe --argument",
			[&](const PROCESS_INFORMATION &) -> std::expected<void, DWORD> {
				ExpectFailure(Run(), Launcher::LaunchStep::Busy, ERROR_BUSY);
				defaultDesktop = true;
				ExpectFailure(Run(L"Default"), Launcher::LaunchStep::Busy, ERROR_BUSY);
				return {};
			},
			Api());
		ASSERT_TRUE(result);
		closed.clear();
		EXPECT_TRUE(Run());
		EXPECT_EQ(creates, 2);
	}
	TEST_F(SecureApplicationLauncher, FailureDoesNotLatchLaunchGuard)
	{
		createError = ERROR_FILE_NOT_FOUND;
		EXPECT_FALSE(Run());
		createError.reset();
		closed.clear();
		EXPECT_TRUE(Run());
	}
	TEST_F(SecureApplicationLauncher, ProcessOnlyEntryPointPreservesNoPolicyBehavior)
	{
		auto result = Internal::CreateApplication(
			L"Secure", L"example.exe --argument",
			[](const PROCESS_INFORMATION &) -> std::expected<void, DWORD> { return {}; },
			Api());
		ASSERT_TRUE(result);
		EXPECT_EQ(*result, 42u);
		EXPECT_EQ(tokenReads, 0);
		EXPECT_EQ(desktopWrites, 0);
		EXPECT_EQ(stationWrites, 0);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Thread(), Process() }));
	}
} // namespace DesktopRanger::Tests
