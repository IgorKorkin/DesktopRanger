#include "pch.h"

#include "internal/desktop_lifecycle_internal.h"
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace DesktopRanger::Tests
{
	namespace Lifecycle = DesktopLifecycle;
	namespace Internal = Lifecycle::detail;
	static_assert(std::is_same_v<
				  decltype(&Lifecycle::CreateSecureDesktop),
				  std::expected<Lifecycle::UniqueDesktop, DWORD> (*)(std::wstring_view)>);

	class DesktopLifecycle : public ::testing::Test {
	protected:
		inline static thread_local DesktopLifecycle *active{};
		static HDESK Original()
		{
			return reinterpret_cast<HDESK>(1);
		}
		static HDESK Target()
		{
			return reinterpret_cast<HDESK>(2);
		}
		static HDESK CallerDesktop()
		{
			return reinterpret_cast<HDESK>(3);
		}
		HDESK bound{ Original() };
		DWORD createError{}, visibleError{}, originalBindError{};
		DWORD bindError{}, readError{};
		int failBindCall{}, failReadCall{};
		int reads{}, creates{}, closes{};
		bool ignoreBinding{};
		bool failVisible{}, failOriginalBind{};
		std::wstring expectedName{ L"secure" };
		std::vector<HDESK> bindings, switches, closed;
		std::string events;

		void SetUp() override
		{
			active = this;
		}
		void TearDown() override
		{
			active = nullptr;
		}

		static HDESK WINAPI Read(DWORD threadId)
		{
			EXPECT_EQ(threadId, ::GetCurrentThreadId());
			++active->reads;
			active->events += 'g';
			if (active->reads == active->failReadCall) {
				::SetLastError(active->readError);
				return nullptr;
			}
			return active->bound;
		}
		static HDESK WINAPI Create(LPCWSTR name, LPCWSTR device, DEVMODEW *mode,
								   DWORD flags, ACCESS_MASK access,
								   SECURITY_ATTRIBUTES *attributes)
		{
			++active->creates;
			active->events += 'c';
			EXPECT_EQ(std::wstring(name), active->expectedName);
			EXPECT_EQ(device, nullptr);
			EXPECT_EQ(mode, nullptr);
			EXPECT_EQ(flags, 0);
			EXPECT_EQ(access, GENERIC_ALL);
			EXPECT_EQ(attributes->nLength, sizeof(SECURITY_ATTRIBUTES));
			EXPECT_EQ(attributes->bInheritHandle, FALSE);
			BOOL present{}, defaulted{};
			PACL dacl{};
			EXPECT_TRUE(::GetSecurityDescriptorDacl(attributes->lpSecurityDescriptor,
													&present, &dacl, &defaulted));
			EXPECT_TRUE(present);
			EXPECT_NE(dacl, nullptr);
			if (dacl) {
				EXPECT_EQ(dacl->AceCount, 0);
			}
			SECURITY_DESCRIPTOR_CONTROL control{};
			DWORD revision{};
			EXPECT_TRUE(::GetSecurityDescriptorControl(attributes->lpSecurityDescriptor,
													   &control, &revision));
			EXPECT_TRUE(control & SE_DACL_PROTECTED);
			if (active->createError) {
				::SetLastError(active->createError);
				return nullptr;
			}
			return Target();
		}
		static BOOL WINAPI Bind(HDESK desktop)
		{
			active->bindings.push_back(desktop);
			active->events += 'b';
			if (static_cast<int>(active->bindings.size()) == active->failBindCall) {
				::SetLastError(active->bindError);
				return FALSE;
			}
			if (desktop == Original() &&
				(active->originalBindError || active->failOriginalBind)) {
				::SetLastError(active->originalBindError);
				return FALSE;
			}
			if (!active->ignoreBinding) {
				active->bound = desktop;
			}
			::SetLastError(ERROR_SUCCESS);
			return TRUE;
		}
		static BOOL WINAPI Switch(HDESK desktop)
		{
			active->switches.push_back(desktop);
			active->events += 'v';
			::SetLastError(active->visibleError);
			return (active->visibleError || active->failVisible) ? FALSE : TRUE;
		}
		static BOOL WINAPI Close(HDESK desktop)
		{
			++active->closes;
			active->closed.push_back(desktop);
			active->events += 'x';
			return TRUE;
		}

		// Only this WIL type ever owns fake handles. Its compile-time deleter is
		// Close above, NOT ::CloseDesktop. Production keeps Lifecycle::UniqueDesktop.
		using TestOwner = wil::unique_any<HDESK, decltype(&Close), Close>;
		static Internal::DesktopApi Api()
		{
			return { &Read, &Create, &Bind, &Switch };
		}
	};

	TEST_F(DesktopLifecycle, CreatesProtectedNonInheritableDenyAllDesktop)
	{
		auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
		ASSERT_TRUE(desktop);
		EXPECT_EQ(desktop->get(), Target());
		EXPECT_EQ(creates, 1);
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, NullTerminatesStringViewBeforeCreate)
	{
		const std::wstring name = L"secure-extra";
		auto desktop = Internal::CreateSecureDesktop<TestOwner>(
			std::wstring_view(name).substr(0, 6), Api());
		ASSERT_TRUE(desktop);
	}
	TEST_F(DesktopLifecycle, RejectsEmptyNameBeforeCreate)
	{
		auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"", Api());
		ASSERT_FALSE(desktop);
		EXPECT_EQ(desktop.error(), ERROR_INVALID_PARAMETER);
		EXPECT_EQ(creates, 0);
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, RejectsEmbeddedNullBeforeCreate)
	{
		auto desktop = Internal::CreateSecureDesktop<TestOwner>(
			std::wstring_view(L"s\0e", 3), Api());
		ASSERT_FALSE(desktop);
		EXPECT_EQ(creates, 0);
	}
	TEST_F(DesktopLifecycle, CreationFailurePreservesErrorAndDoesNotClose)
	{
		createError = ERROR_ACCESS_DENIED;
		auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
		ASSERT_FALSE(desktop);
		EXPECT_EQ(desktop.error(), createError);
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, OwnedHandleClosesExactlyOnceOnScopeExit)
	{
		{
			auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
			ASSERT_TRUE(desktop);
		}
		EXPECT_EQ(closes, 1);
		EXPECT_EQ(closed.front(), Target());
	}
	TEST_F(DesktopLifecycle, MoveTransfersOwnershipWithoutDoubleClose)
	{
		{
			auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
			ASSERT_TRUE(desktop);
			TestOwner moved{ std::move(desktop.value()) };
			EXPECT_FALSE(static_cast<bool>(desktop.value()));
			EXPECT_EQ(moved.get(), Target());
		}
		EXPECT_EQ(closes, 1);
	}
	TEST_F(DesktopLifecycle, ReleaseTransfersOwnershipToCaller)
	{
		HDESK released{};
		{
			auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
			ASSERT_TRUE(desktop);
			released = desktop->release();
		}
		EXPECT_EQ(closes, 0);
		{
			TestOwner caller{ released };
		}
		EXPECT_EQ(closes, 1);
	}
	TEST_F(DesktopLifecycle, EmptyOwnerDoesNotClose)
	{
		{
			TestOwner empty;
		}
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, ExceptionUnwindingClosesOwnedHandle)
	{
		EXPECT_THROW(
			{
				auto desktop = Internal::CreateSecureDesktop<TestOwner>(L"secure", Api());
				ASSERT_TRUE(desktop);
				throw std::runtime_error("worker creation failed");
			},
			std::runtime_error);
		EXPECT_EQ(closes, 1);
	}
	TEST_F(DesktopLifecycle, CurrentDesktopIsBorrowedAndNeverClosed)
	{
		{
			const auto original = Internal::GetCurrentDesktop(Api());
			ASSERT_TRUE(original);
			EXPECT_EQ(original.value(), Original());
		}
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, CurrentDesktopFailurePreservesError)
	{
		failReadCall = 1;
		readError = ERROR_INVALID_HANDLE;
		auto original = Internal::GetCurrentDesktop(Api());
		ASSERT_FALSE(original);
		EXPECT_EQ(original.error(), readError);
	}
	TEST_F(DesktopLifecycle, SuccessfulSwitchReturnsBorrowedOriginal)
	{
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_TRUE(result);
		EXPECT_EQ(result.value(), Original());
		EXPECT_EQ(bound, Target());
		EXPECT_EQ(events, "gbgv");
		EXPECT_EQ(switches, std::vector<HDESK>{ Target() });
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, NullSwitchTargetMakesNoCalls)
	{
		auto result = Internal::SwitchToDesktop(nullptr, Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().error, ERROR_INVALID_HANDLE);
		EXPECT_TRUE(events.empty());
	}
	TEST_F(DesktopLifecycle, CannotReadOriginalDoesNotBindOrSwitch)
	{
		failReadCall = 1;
		readError = ERROR_ACCESS_DENIED;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().step, Lifecycle::SwitchStep::GetOriginal);
		EXPECT_EQ(result.error().error, readError);
		EXPECT_EQ(events, "g");
	}
	TEST_F(DesktopLifecycle, BindFailureDoesNotSwitchOrAttemptRollback)
	{
		failBindCall = 1;
		bindError = ERROR_BUSY;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().step, Lifecycle::SwitchStep::SetThread);
		EXPECT_EQ(result.error().error, bindError);
		EXPECT_FALSE(result.error().rollbackError);
		EXPECT_EQ(events, "gb");
	}
	TEST_F(DesktopLifecycle, BindingMismatchAttemptsRollbackBeforeReturning)
	{
		ignoreBinding = true;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().step, Lifecycle::SwitchStep::VerifyThread);
		EXPECT_EQ(result.error().error, ERROR_INVALID_STATE);
		EXPECT_FALSE(result.error().rollbackError);
		EXPECT_EQ(events, "gbgb");
	}
	TEST_F(DesktopLifecycle, VerificationQueryErrorSurvivesSuccessfulRollback)
	{
		failReadCall = 2;
		readError = ERROR_INVALID_HANDLE;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().error, readError);
		EXPECT_EQ(bound, Original());
		EXPECT_FALSE(result.error().rollbackError);
	}
	TEST_F(DesktopLifecycle, VisibleSwitchFailureSurvivesSuccessfulRollback)
	{
		visibleError = ERROR_ACCESS_DENIED;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().step, Lifecycle::SwitchStep::SwitchVisible);
		EXPECT_EQ(result.error().error, visibleError);
		EXPECT_FALSE(result.error().rollbackError);
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(events, "gbgvb");
	}
	TEST_F(DesktopLifecycle, SwitchAndRollbackErrorsRemainDistinct)
	{
		visibleError = ERROR_ACCESS_DENIED;
		originalBindError = ERROR_BUSY;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().error, ERROR_ACCESS_DENIED);
		EXPECT_EQ(result.error().rollbackError, ERROR_BUSY);
		EXPECT_EQ(bound, Target());
	}
	TEST_F(DesktopLifecycle, VerificationAndRollbackErrorsRemainDistinct)
	{
		ignoreBinding = true;
		originalBindError = ERROR_BUSY;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().error, ERROR_INVALID_STATE);
		EXPECT_EQ(result.error().rollbackError, ERROR_BUSY);
	}
	TEST_F(DesktopLifecycle, SwitchingToCurrentDesktopDoesNotTakeOwnership)
	{
		auto result = Internal::SwitchToDesktop(Original(), Api());
		ASSERT_TRUE(result);
		EXPECT_EQ(closes, 0);
		EXPECT_EQ(events, "gbgv");
	}
	TEST_F(DesktopLifecycle, RestorationSwitchesVisibleDesktopBeforeRebinding)
	{
		bound = Target();
		ASSERT_TRUE(Internal::RestoreDesktop(Original(), Api()));
		EXPECT_EQ(events, "vbg");
		EXPECT_EQ(switches, std::vector<HDESK>{ Original() });
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, RestoresCallerVisibleDesktopAndWorkerBindingSeparately)
	{
		bound = Target();
		ASSERT_TRUE(Internal::RestoreDesktop(CallerDesktop(), Original(), Api()));
		EXPECT_EQ(switches, std::vector<HDESK>{ CallerDesktop() });
		EXPECT_EQ(bindings, std::vector<HDESK>{ Original() });
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, SeparateRestoreHandlesRetainIndependentErrors)
	{
		visibleError = ERROR_ACCESS_DENIED;
		originalBindError = ERROR_BUSY;
		auto result = Internal::RestoreDesktop(CallerDesktop(), Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().switchError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(result.error().threadError, ERROR_BUSY);
		EXPECT_EQ(switches, std::vector<HDESK>{ CallerDesktop() });
		EXPECT_EQ(bindings, std::vector<HDESK>{ Original() });
	}
	TEST_F(DesktopLifecycle, FailedSwitchRollbackCanBeRetriedWithBorrowedOriginal)
	{
		visibleError = ERROR_ACCESS_DENIED;
		originalBindError = ERROR_BUSY;
		const auto switched = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(switched);
		EXPECT_EQ(switched.error().rollbackError, ERROR_BUSY);
		EXPECT_EQ(closes, 0);

		visibleError = 0;
		originalBindError = 0;
		const auto restored =
			Internal::RestoreDesktop(CallerDesktop(), Original(), Api());
		ASSERT_TRUE(restored);
		EXPECT_EQ(switches.back(), CallerDesktop());
		EXPECT_EQ(bindings.back(), Original());
		EXPECT_EQ(closes, 0);
	}
	TEST_F(DesktopLifecycle, RestoreStillRebindsAfterVisibleFailure)
	{
		bound = Target();
		visibleError = ERROR_ACCESS_DENIED;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().switchError, ERROR_ACCESS_DENIED);
		EXPECT_FALSE(result.error().threadError);
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(events, "vbg");
	}
	TEST_F(DesktopLifecycle, RestoreReportsBindingFailureAfterVisibleSuccess)
	{
		bound = Target();
		originalBindError = ERROR_BUSY;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_FALSE(result.error().switchError);
		EXPECT_EQ(result.error().threadError, ERROR_BUSY);
		EXPECT_EQ(events, "vb");
		EXPECT_EQ(switches, std::vector<HDESK>{ Original() });
	}
	TEST_F(DesktopLifecycle, RestorePreservesBothIndependentErrors)
	{
		visibleError = ERROR_ACCESS_DENIED;
		originalBindError = ERROR_BUSY;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().switchError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(result.error().threadError, ERROR_BUSY);
	}
	TEST_F(DesktopLifecycle, RestoreDetectsBindingMismatch)
	{
		bound = Target();
		ignoreBinding = true;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().threadError, ERROR_INVALID_STATE);
	}
	TEST_F(DesktopLifecycle, RestorePreservesVerificationQueryError)
	{
		failReadCall = 1;
		readError = ERROR_INVALID_HANDLE;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().threadError, readError);
	}
	TEST_F(DesktopLifecycle, NullRestoreTargetMakesNoCalls)
	{
		auto result = Internal::RestoreDesktop(nullptr, Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().switchError, ERROR_INVALID_HANDLE);
		EXPECT_EQ(result.error().threadError, ERROR_INVALID_HANDLE);
		EXPECT_TRUE(events.empty());
	}
	TEST_F(DesktopLifecycle, RestorationCanBeRetriedAfterTransientBindingFailure)
	{
		bound = Target();
		originalBindError = ERROR_BUSY;
		EXPECT_FALSE(Internal::RestoreDesktop(Original(), Api()));
		originalBindError = 0;
		EXPECT_TRUE(Internal::RestoreDesktop(Original(), Api()));
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(closes, 0);
	}

	TEST_F(DesktopLifecycle, RestoreVisibleFailureWithZeroLastErrorIsNotSuccess)
	{
		failVisible = true;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		ASSERT_TRUE(result.error().switchError.has_value());
		EXPECT_EQ(*result.error().switchError, 0);
		EXPECT_FALSE(result.error().threadError);
		EXPECT_EQ(switches, std::vector<HDESK>{ Original() });
	}

	TEST_F(DesktopLifecycle, RestoreBindingFailureWithZeroLastErrorIsNotSuccess)
	{
		failOriginalBind = true;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		EXPECT_FALSE(result.error().switchError);
		ASSERT_TRUE(result.error().threadError.has_value());
		EXPECT_EQ(*result.error().threadError, 0);
	}

	TEST_F(DesktopLifecycle, RestoreVerificationFailureWithZeroLastErrorIsNotSuccess)
	{
		failReadCall = 1;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		ASSERT_TRUE(result.error().threadError.has_value());
		EXPECT_EQ(*result.error().threadError, 0);
	}

	TEST_F(DesktopLifecycle, RestoreRetainsBothFailuresWithZeroLastError)
	{
		failVisible = true;
		failOriginalBind = true;
		auto result = Internal::RestoreDesktop(Original(), Api());
		ASSERT_FALSE(result);
		ASSERT_TRUE(result.error().switchError.has_value());
		ASSERT_TRUE(result.error().threadError.has_value());
		EXPECT_EQ(*result.error().switchError, 0);
		EXPECT_EQ(*result.error().threadError, 0);
	}

	TEST_F(DesktopLifecycle, SwitchAndRollbackRetainFailuresWithZeroLastError)
	{
		failVisible = true;
		failOriginalBind = true;
		auto result = Internal::SwitchToDesktop(Target(), Api());
		ASSERT_FALSE(result);
		EXPECT_EQ(result.error().error, 0);
		ASSERT_TRUE(result.error().rollbackError.has_value());
		EXPECT_EQ(*result.error().rollbackError, 0);
		EXPECT_EQ(switches, std::vector<HDESK>{ Target() });
	}
} // namespace DesktopRanger::Tests
