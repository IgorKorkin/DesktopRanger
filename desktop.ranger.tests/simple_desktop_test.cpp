#include "pch.h"

#include "../simple.desktop.console/internal/simple_desktop_internal.h"

#include <string>
#include <vector>

namespace DesktopRanger::Tests
{
	namespace Simple = SimpleSecureDesktop;
	namespace Lifecycle = DesktopRanger::DesktopLifecycle;

	class SimpleDesktop : public ::testing::Test {
	protected:
		inline static thread_local SimpleDesktop *active{};
		std::string events, diagnostics;
		bool failCreate{}, failPrompt{}, failNotification{}, failRead{}, failSwitch{};
		DWORD createError{ ERROR_FILE_NOT_FOUND }, promptError{ ERROR_ACCESS_DENIED };
		DWORD notificationError{ ERROR_NOT_ENOUGH_MEMORY }, destroyError{}, borderError{};
		DWORD waitResult{}, borderThread{};
		int waits{}, restores{}, notifications{};
		std::vector<HANDLE> closed;
		std::vector<Lifecycle::RestoreError> restoreResults;
		Simple::RunError errors{};

		static HDESK Target()
		{
			return reinterpret_cast<HDESK>(1);
		}
		static HDESK Visible()
		{
			return reinterpret_cast<HDESK>(2);
		}
		static HDESK Original()
		{
			return reinterpret_cast<HDESK>(3);
		}
		static HWND Border()
		{
			return reinterpret_cast<HWND>(7);
		}
		static HANDLE Process()
		{
			return reinterpret_cast<HANDLE>(10);
		}
		static HANDLE Thread()
		{
			return reinterpret_cast<HANDLE>(11);
		}

		void SetUp() override
		{
			active = this;
		}
		void TearDown() override
		{
			active = nullptr;
		}

		static BOOL WINAPI Create(LPCWSTR, LPWSTR command, LPSECURITY_ATTRIBUTES,
								  LPSECURITY_ATTRIBUTES, BOOL inherit, DWORD flags,
								  LPVOID, LPCWSTR, LPSTARTUPINFOW startup,
								  LPPROCESS_INFORMATION info)
		{
			active->events += 'c';
			EXPECT_STREQ(command, L"explorer shell:AppsFolder");
			EXPECT_STREQ(startup->lpDesktop, L"SimpleSecretDesktop");
			EXPECT_EQ(inherit, FALSE);
			EXPECT_EQ(flags, 0);
			if (active->failCreate) {
				::SetLastError(active->createError);
				return FALSE;
			}
			info->hProcess = Process();
			info->hThread = Thread();
			return TRUE;
		}
		static DWORD WINAPI Wait(HANDLE process, DWORD timeout)
		{
			EXPECT_EQ(process, Process());
			EXPECT_EQ(timeout, 200);
			++active->waits;
			return active->waitResult;
		}
		static BOOL WINAPI Close(HANDLE handle)
		{
			active->closed.push_back(handle);
			return TRUE;
		}
		static int WINAPI Message(HWND owner, LPCWSTR text, LPCWSTR, UINT flags)
		{
			EXPECT_EQ(owner, nullptr);
			if ((flags & MB_ICONMASK) == MB_ICONERROR) {
				active->events += 'n';
				++active->notifications;
				EXPECT_NE(flags & MB_SERVICE_NOTIFICATION, 0);
				EXPECT_EQ(flags & MB_DEFAULT_DESKTOP_ONLY, 0);
				EXPECT_NE(std::wstring(text).find(L"restoration failed"),
						  std::wstring::npos);
				if (active->failNotification) {
					::SetLastError(active->notificationError);
					return 0;
				}
			} else {
				active->events += 'm';
				if (active->failPrompt) {
					::SetLastError(active->promptError);
					return 0;
				}
			}
			return IDOK;
		}
		static std::expected<HDESK, DWORD> Get() noexcept
		{
			active->events += 'g';
			if (active->failRead) {
				return std::unexpected(ERROR_INVALID_HANDLE);
			}
			return Original();
		}
		static std::expected<HDESK, Lifecycle::SwitchError> Switch(HDESK desktop) noexcept
		{
			EXPECT_EQ(desktop, Target());
			active->events += 's';
			if (active->failSwitch) {
				return std::unexpected(
					Lifecycle::SwitchError{ Lifecycle::SwitchStep::SwitchVisible,
											ERROR_ACCESS_DENIED, ERROR_BUSY });
			}
			return Original();
		}
		static std::expected<void, Lifecycle::RestoreError> Restore(HDESK visible,
																	HDESK thread) noexcept
		{
			EXPECT_EQ(visible, Visible());
			EXPECT_EQ(thread, Original());
			active->events += 'r';
			const auto index = active->restores++;
			if (index < static_cast<int>(active->restoreResults.size())) {
				const auto failure = active->restoreResults[index];
				if (failure.switchError || failure.threadError) {
					return std::unexpected(failure);
				}
			}
			return {};
		}
		static std::expected<std::vector<HWND>, DWORD> CreateBorders(COLORREF color)
		{
			EXPECT_EQ(color, RGB(255, 255, 0));
			active->events += 'b';
			active->borderThread = ::GetCurrentThreadId();
			if (active->borderError) {
				return std::unexpected(active->borderError);
			}
			return std::vector<HWND>{ Border() };
		}
		static std::expected<void, DWORD> DestroyBorders(std::span<const HWND> windows)
		{
			EXPECT_EQ(active->borderThread, ::GetCurrentThreadId());
			EXPECT_EQ(windows.size(), 1);
			EXPECT_EQ(windows.front(), Border());
			active->events += 'd';
			if (active->destroyError) {
				return std::unexpected(active->destroyError);
			}
			return {};
		}
		static Simple::detail::SimpleApi Api()
		{
			return { &Create, &Wait,	&Close,			&Message,		&Get,
					 &Switch, &Restore, &CreateBorders, &DestroyBorders };
		}
		std::expected<void, DWORD> Launch()
		{
			wchar_t command[] = L"explorer shell:AppsFolder";
			return Simple::detail::LaunchAppOnDesktop(L"SimpleSecretDesktop", command,
													  Api());
		}
		void Run()
		{
			::testing::internal::CaptureStderr();
			Simple::detail::RunDesktopWorker(Target(), Visible(), errors, Api());
			diagnostics = ::testing::internal::GetCapturedStderr();
		}
	};

	TEST_F(SimpleDesktop, CreateFailureReturnsErrorWithoutClosingUnownedHandles)
	{
		failCreate = true;
		const auto result = Launch();
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), ERROR_FILE_NOT_FOUND);
		EXPECT_EQ(waits, 0);
		EXPECT_TRUE(closed.empty());
	}
	TEST_F(SimpleDesktop, CreateFailureWithZeroLastErrorUsesFallback)
	{
		failCreate = true;
		createError = 0;
		const auto result = Launch();
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error(), ERROR_GEN_FAILURE);
	}
	TEST_F(SimpleDesktop, SuccessfulLaunchClosesProcessAndThread)
	{
		ASSERT_TRUE(Launch());
		EXPECT_EQ(waits, 1);
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Thread(), Process() }));
	}
	TEST_F(SimpleDesktop, IdleWaitFailureStillClosesProcessAndThread)
	{
		waitResult = WAIT_FAILED;
		ASSERT_TRUE(Launch());
		EXPECT_EQ(closed, (std::vector<HANDLE>{ Thread(), Process() }));
	}
	TEST_F(SimpleDesktop, SuccessDestroysBordersBeforeRestoration)
	{
		Run();
		EXPECT_EQ(events, "gsbmdr");
		EXPECT_FALSE(errors.operationError);
		EXPECT_FALSE(errors.visibleRestoreError);
		EXPECT_FALSE(errors.threadRestoreError);
	}
	TEST_F(SimpleDesktop, MissingWorkerDesktopStopsBeforeSwitch)
	{
		failRead = true;
		Run();
		EXPECT_EQ(events, "g");
		EXPECT_EQ(errors.operationError, ERROR_INVALID_HANDLE);
	}
	TEST_F(SimpleDesktop, SwitchFailurePreservesRollbackAndRestoresBorrowedHandles)
	{
		failSwitch = true;
		Run();
		EXPECT_EQ(events, "gsr");
		EXPECT_EQ(errors.operationError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(errors.rollbackError, ERROR_BUSY);
	}
	TEST_F(SimpleDesktop, BorderCreationFailureStillRestoresDesktop)
	{
		borderError = ERROR_NOT_ENOUGH_MEMORY;
		Run();
		EXPECT_EQ(events, "gsbr");
		EXPECT_EQ(errors.operationError, borderError);
	}
	TEST_F(SimpleDesktop, FailedPromptReturnsErrorAndCleansUpBeforeRestoration)
	{
		failPrompt = true;
		Run();
		EXPECT_EQ(events, "gsbmdr");
		EXPECT_EQ(errors.operationError, ERROR_ACCESS_DENIED);
	}
	TEST_F(SimpleDesktop, FailedPromptWithZeroLastErrorUsesFallback)
	{
		failPrompt = true;
		promptError = 0;
		Run();
		EXPECT_EQ(errors.operationError, ERROR_GEN_FAILURE);
		EXPECT_EQ(restores, 1);
	}
	TEST_F(SimpleDesktop, TransientRestoreFailureRetriesAfterWindowDestruction)
	{
		restoreResults = { { ERROR_ACCESS_DENIED, std::nullopt } };
		Run();
		EXPECT_EQ(events, "gsbmdrr");
		EXPECT_EQ(errors.visibleRestoreError, ERROR_ACCESS_DENIED);
		EXPECT_NE(diagnostics.find("retry succeeded"), std::string::npos);
		EXPECT_EQ(notifications, 0);
	}
	TEST_F(SimpleDesktop, PersistentRestoreFailureNotifiesActiveDesktopAfterRetry)
	{
		restoreResults = { { ERROR_ACCESS_DENIED, ERROR_BUSY },
						   { ERROR_INVALID_HANDLE, ERROR_BUSY } };
		Run();
		EXPECT_EQ(events, "gsbmdrrn");
		EXPECT_EQ(errors.visibleRestoreError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(errors.threadRestoreError, ERROR_BUSY);
		EXPECT_NE(diagnostics.find("retry failed"), std::string::npos);
		EXPECT_NE(diagnostics.find("visible=6"), std::string::npos);
		EXPECT_EQ(notifications, 1);
	}
	TEST_F(SimpleDesktop, FailedNotificationPreservesOperationAndRestorationErrors)
	{
		failPrompt = true;
		failNotification = true;
		restoreResults = { { ERROR_BUSY, std::nullopt }, { ERROR_BUSY, std::nullopt } };
		Run();
		EXPECT_EQ(events, "gsbmdrrn");
		EXPECT_EQ(errors.operationError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(errors.visibleRestoreError, ERROR_BUSY);
		EXPECT_EQ(errors.notificationError, ERROR_NOT_ENOUGH_MEMORY);
		EXPECT_NE(diagnostics.find("notification failed"), std::string::npos);
	}
	TEST_F(SimpleDesktop, FailedNotificationWithZeroLastErrorUsesFallback)
	{
		failNotification = true;
		notificationError = 0;
		restoreResults = { { ERROR_BUSY, std::nullopt }, { ERROR_BUSY, std::nullopt } };
		Run();
		EXPECT_EQ(errors.notificationError, ERROR_GEN_FAILURE);
	}
	TEST_F(SimpleDesktop, WindowCleanupFailureStillRestoresDesktop)
	{
		destroyError = ERROR_ACCESS_DENIED;
		Run();
		EXPECT_EQ(events, "gsbmdr");
		EXPECT_EQ(errors.windowCleanupError, ERROR_ACCESS_DENIED);
	}
	TEST_F(SimpleDesktop, NextWorkerRunSucceedsAfterPromptFailure)
	{
		failPrompt = true;
		Run();
		ASSERT_TRUE(errors.operationError);
		failPrompt = false;
		errors = {};
		events.clear();
		Run();
		EXPECT_EQ(events, "gsbmdr");
		EXPECT_FALSE(errors.operationError);
	}
	TEST_F(SimpleDesktop, BindingRestoreFailureRetriesSeparateOriginalHandles)
	{
		restoreResults = { { std::nullopt, ERROR_BUSY } };
		Run();
		EXPECT_EQ(restores, 2);
		EXPECT_FALSE(errors.visibleRestoreError);
		EXPECT_EQ(errors.threadRestoreError, ERROR_BUSY);
	}
	TEST_F(SimpleDesktop, OperationErrorIsSeparateFromBothRestoreErrors)
	{
		failSwitch = true;
		restoreResults = { { ERROR_INVALID_HANDLE, ERROR_BUSY } };
		Run();
		EXPECT_EQ(errors.operationError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(errors.rollbackError, ERROR_BUSY);
		EXPECT_EQ(errors.visibleRestoreError, ERROR_INVALID_HANDLE);
		EXPECT_EQ(errors.threadRestoreError, ERROR_BUSY);
		EXPECT_EQ(events, "gsrr");
	}
} // namespace DesktopRanger::Tests
