#include "pch.h"

#include "../desktop.ranger.functional/internal/functional_runner_internal.h"
#include "internal/secure_application_launcher_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace DesktopRanger::Tests
{
	namespace Functional = DesktopRanger::Functional;
	namespace Internal = Functional::detail;
	namespace Launcher = DesktopRanger::SecureApplicationLauncher;

	class FunctionalRunner : public ::testing::Test {
	protected:
		inline static thread_local FunctionalRunner *active{};
		Functional::Options options{ L"test_probe.exe", L"simulated", 100, 50, 25 };
		bool running{}, stopSet{}, stopped{}, failCreate{}, ignoreStop{}, failTerminate{};
		bool failSet{}, failExitCode{}, acknowledgeStopped{ true }, failAlive{},
			failEnter{}, failGet{};
		DWORD readiness{ WAIT_OBJECT_0 + 1 }, lastError{ ERROR_ACCESS_DENIED };
		DWORD createError{ ERROR_FILE_NOT_FOUND }, probeExit{};
		int restoreFailures{}, restoreCalls{}, processCloses{}, threadCloses{}, kills{};
		ULONGLONG ticks{ 100 }, launchDelay{};
		HDESK bound{ Original() };
		std::vector<std::string> events;

		static HDESK Original()
		{
			return reinterpret_cast<HDESK>(1);
		}
		static HDESK Target()
		{
			return reinterpret_cast<HDESK>(2);
		}
		static HDESK Visible()
		{
			return reinterpret_cast<HDESK>(3);
		}
		static HANDLE Process()
		{
			return reinterpret_cast<HANDLE>(10);
		}
		static HANDLE Thread()
		{
			return reinterpret_cast<HANDLE>(11);
		}
		static HANDLE Ready()
		{
			return reinterpret_cast<HANDLE>(20);
		}
		static HANDLE Stop()
		{
			return reinterpret_cast<HANDLE>(21);
		}
		static HANDLE Stopped()
		{
			return reinterpret_cast<HANDLE>(22);
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
								  LPPROCESS_INFORMATION process)
		{
			active->events.emplace_back("create");
			EXPECT_EQ(inherit, FALSE);
			EXPECT_EQ(flags, 0);
			EXPECT_STREQ(startup->lpDesktop, L"simulated");
			EXPECT_NE(std::wstring(command).find(L"test_probe.exe"), std::wstring::npos);
			if (active->failCreate) {
				::SetLastError(active->createError);
				return FALSE;
			}
			active->running = true;
			active->ticks += active->launchDelay;
			process->hProcess = Process();
			process->hThread = Thread();
			process->dwProcessId = 123;
			process->dwThreadId = 124;
			return TRUE;
		}
		static BOOL WINAPI Close(HANDLE handle)
		{
			if (handle == Process()) {
				++active->processCloses;
				active->events.emplace_back("close_process");
			} else if (handle == Thread())
				++active->threadCloses;
			else
				ADD_FAILURE() << "An unowned/fake Desktop or event reached CloseHandle";
			return TRUE;
		}
		static std::expected<DWORD, Launcher::LaunchError> Launch(
			std::wstring_view desktop, std::wstring_view command,
			const std::function<std::expected<void, DWORD>(const PROCESS_INFORMATION &)>
				&callback)
		{
			Launcher::detail::LaunchApi api;
			api.createProcess = &Create;
			api.closeHandle = &Close;
			// Real Core implementation owns/closes process handles and invokes runner's
			// callback.
			return Launcher::detail::CreateApplication(desktop, command, callback, api);
		}
		static DWORD WINAPI WaitMany(DWORD count, const HANDLE *handles, BOOL all,
									 DWORD timeout)
		{
			EXPECT_EQ(count, 2);
			EXPECT_EQ(handles[0], Process()); // An exited process wins over ready.
			EXPECT_EQ(handles[1], Ready());
			EXPECT_FALSE(all);
			EXPECT_LE(timeout, active->options.readyTimeoutMs);
			active->events.emplace_back("wait_ready");
			if (active->readiness == WAIT_TIMEOUT)
				active->ticks += timeout;
			else
				active->ticks += 2;
			if (active->readiness == WAIT_OBJECT_0)
				active->running = false;
			if (active->readiness == WAIT_FAILED)
				::SetLastError(active->lastError);
			return active->readiness;
		}
		static DWORD WINAPI WaitOne(HANDLE handle, DWORD timeout)
		{
			EXPECT_NE(timeout, INFINITE);
			if (handle == Stopped())
				return active->stopped ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
			EXPECT_EQ(handle, Process());
			if (!active->running)
				return WAIT_OBJECT_0;
			if (!timeout) {
				if (active->failAlive) {
					::SetLastError(active->lastError);
					return WAIT_FAILED;
				}
				return WAIT_TIMEOUT;
			}
			if (active->stopSet && !active->ignoreStop) {
				active->running = false;
				active->stopped = active->acknowledgeStopped;
				active->ticks += 3;
				active->events.emplace_back("probe_exited");
				return WAIT_OBJECT_0;
			}
			active->ticks += timeout;
			return WAIT_TIMEOUT;
		}
		static BOOL WINAPI Set(HANDLE handle)
		{
			EXPECT_EQ(handle, Stop());
			active->events.emplace_back("stop");
			if (active->failSet) {
				::SetLastError(active->lastError);
				return FALSE;
			}
			active->stopSet = true;
			return TRUE;
		}
		static BOOL WINAPI Terminate(HANDLE handle, UINT)
		{
			EXPECT_EQ(handle, Process());
			++active->kills;
			if (active->failTerminate) {
				::SetLastError(active->lastError);
				return FALSE;
			}
			active->running = false;
			return TRUE;
		}
		static BOOL WINAPI ExitCode(HANDLE handle, LPDWORD code)
		{
			EXPECT_EQ(handle, Process());
			if (active->failExitCode) {
				::SetLastError(active->lastError);
				return FALSE;
			}
			*code = active->probeExit;
			return TRUE;
		}
		static ULONGLONG WINAPI Now()
		{
			return active->ticks;
		}
		static HDESK WINAPI Get(DWORD)
		{
			if (active->failGet) {
				::SetLastError(active->lastError);
				return nullptr;
			}
			return active->bound;
		}
		static BOOL WINAPI Bind(HDESK desktop)
		{
			active->bound = desktop;
			return TRUE;
		}
		static BOOL WINAPI Switch(HDESK desktop)
		{
			if (desktop == Visible()) {
				active->events.emplace_back("restore_visible");
				++active->restoreCalls;
				if (active->restoreCalls <= active->restoreFailures) {
					::SetLastError(active->lastError);
					return FALSE;
				}
			} else {
				EXPECT_EQ(desktop, Target());
				if (active->failEnter) {
					::SetLastError(active->lastError);
					return FALSE;
				}
			}
			return TRUE;
		}
		Functional::Result
		Run(Functional::Scenario scenario = Functional::Scenario::LaunchReady,
			bool desktop = true)
		{
			Internal::Api api{ &Launch,	   &WaitMany, &WaitOne, &Set,
							   &Terminate, &ExitCode, &Now };
			Internal::Signals signals{ Ready(),	 Stop(),  Stopped(),
									   L"ready", L"stop", L"stopped" };
			Internal::SimulatedDesktop simulated{ .target = Target(),
												  .visibleOriginal = Visible() };
			simulated.api.getThreadDesktop = &Get;
			simulated.api.setThreadDesktop = &Bind;
			simulated.api.switchDesktop = &Switch;
			return Internal::RunScenario(scenario, options, signals,
										 api,
										 desktop ? &simulated : nullptr);
		}
	};

	TEST_F(FunctionalRunner,
		   ReadyRequiresSignalThenStopsAndClosesBothHandlesBeforeRestoration)
	{
		const auto result = Run();
		EXPECT_TRUE(result.passed);
		EXPECT_TRUE(result.readyConfirmed);
		EXPECT_TRUE(result.stoppedConfirmed);
		EXPECT_EQ(result.evidence, Functional::Evidence::Simulation);
		EXPECT_EQ(result.processId, 123);
		EXPECT_GT(result.elapsedMs, 0);
		EXPECT_EQ(processCloses, 1);
		EXPECT_EQ(threadCloses, 1);
		EXPECT_EQ(bound, Original());
		EXPECT_LT(std::ranges::find(events, "probe_exited"),
				  std::ranges::find(events, "close_process"));
		EXPECT_LT(std::ranges::find(events, "close_process"),
				  std::ranges::find(events, "restore_visible"));
	}
	TEST_F(FunctionalRunner, ShutdownChecksWindowAcknowledgmentAndExitCode)
	{
		const auto result = Run(Functional::Scenario::ShutdownCleanup);
		EXPECT_TRUE(result.passed);
		EXPECT_TRUE(result.stoppedConfirmed);
		EXPECT_EQ(std::ranges::count(events, "stop"), 1);
	}
	TEST_F(FunctionalRunner, TimeoutScenarioPassesOnlyForReadinessTimeoutWithCleanup)
	{
		readiness = WAIT_TIMEOUT;
		const auto result = Run(Functional::Scenario::ReadinessTimeout);
		EXPECT_TRUE(result.passed);
		EXPECT_EQ(result.expectedError, ERROR_TIMEOUT);
		EXPECT_EQ(result.observedError, ERROR_TIMEOUT);
		EXPECT_FALSE(result.readyConfirmed);
		EXPECT_TRUE(result.readinessTimedOut);
		EXPECT_TRUE(result.stoppedConfirmed);
		EXPECT_FALSE(running);
	}
	TEST_F(FunctionalRunner, CreateProcessSuccessWithoutReadySignalFailsPositiveScenario)
	{
		readiness = WAIT_TIMEOUT;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_TIMEOUT);
		EXPECT_FALSE(result.readyConfirmed);
		EXPECT_EQ(result.processId, 123);
		EXPECT_EQ(processCloses, 1);
	}
	TEST_F(FunctionalRunner, ReadySignalInNegativeScenarioDoesNotPass)
	{
		const auto result = Run(Functional::Scenario::ReadinessTimeout);
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_INVALID_STATE);
	}
	TEST_F(FunctionalRunner, LaunchFailureIsNotMistakenForExpectedReadinessTimeout)
	{
		failCreate = true;
		createError = ERROR_TIMEOUT;
		const auto result = Run(Functional::Scenario::ReadinessTimeout);
		EXPECT_FALSE(result.passed);
		EXPECT_FALSE(result.readinessTimedOut);
		EXPECT_EQ(result.observedError, ERROR_TIMEOUT);
		EXPECT_EQ(result.processId, 0);
		EXPECT_EQ(processCloses, 0);
		EXPECT_EQ(bound, Original());
	}
	TEST_F(FunctionalRunner, LaunchErrorAndDesktopRestoreErrorRemainIndependent)
	{
		failCreate = true;
		restoreFailures = 1;
		const auto result = Run();
		EXPECT_EQ(result.observedError, ERROR_FILE_NOT_FOUND);
		EXPECT_EQ(result.restoreError.switchError, ERROR_ACCESS_DENIED);
		EXPECT_FALSE(result.restoreRetryError.switchError);
		EXPECT_EQ(restoreCalls, 2);
		EXPECT_FALSE(result.passed);
	}
	TEST_F(FunctionalRunner, ZeroLastErrorLaunchFailureIsStillFailure)
	{
		failCreate = true;
		createError = 0;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_GEN_FAILURE);
	}
	TEST_F(FunctionalRunner, EarlyExitTakesPriorityOverReadyAndDoesNotPassTimeoutScenario)
	{
		readiness = WAIT_OBJECT_0;
		const auto result = Run(Functional::Scenario::ReadinessTimeout);
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_PROCESS_ABORTED);
		EXPECT_FALSE(result.readyConfirmed);
		EXPECT_EQ(processCloses, 1);
	}
	TEST_F(FunctionalRunner, ReadinessWaitFailureIsReportedAndResourcesAreClosed)
	{
		readiness = WAIT_FAILED;
		const auto result = Run();
		EXPECT_EQ(result.observedError, ERROR_ACCESS_DENIED);
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(processCloses, 1);
		EXPECT_TRUE(result.stoppedConfirmed);
	}
	TEST_F(FunctionalRunner, ZeroLastErrorWaitFailureUsesFallback)
	{
		readiness = WAIT_FAILED;
		lastError = 0;
		EXPECT_EQ(Run().observedError, ERROR_GEN_FAILURE);
	}
	TEST_F(FunctionalRunner, LateCallbackCannotExtendReadinessDeadline)
	{
		launchDelay = options.readyTimeoutMs + 1;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_TIMEOUT);
		EXPECT_TRUE(result.readinessTimedOut);
		EXPECT_EQ(std::ranges::count(events, "wait_ready"), 0);
	}
	TEST_F(FunctionalRunner, MissingWindowDestructionAcknowledgmentFailsShutdown)
	{
		acknowledgeStopped = false;
		const auto result = Run(Functional::Scenario::ShutdownCleanup);
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.cleanupError, ERROR_INVALID_STATE);
	}
	TEST_F(FunctionalRunner, NonzeroProbeExitIsNotSuccessfulCleanup)
	{
		probeExit = ERROR_INVALID_DATA;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.cleanupError, ERROR_INVALID_DATA);
	}
	TEST_F(FunctionalRunner, ExitCodeQueryFailureIsReported)
	{
		failExitCode = true;
		EXPECT_EQ(Run().cleanupError, ERROR_ACCESS_DENIED);
	}
	TEST_F(FunctionalRunner, UnresponsiveProbeIsTerminatedWithBoundedCleanup)
	{
		ignoreStop = true;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.cleanupError, ERROR_TIMEOUT);
		EXPECT_EQ(result.terminationError, 0);
		EXPECT_EQ(kills, 1);
		EXPECT_FALSE(running);
		EXPECT_EQ(processCloses, 1);
	}
	TEST_F(FunctionalRunner, FailedEmergencyTerminationIsReportedSeparately)
	{
		ignoreStop = true;
		failTerminate = true;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.cleanupError, ERROR_TIMEOUT);
		EXPECT_EQ(result.terminationError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(processCloses, 1);
		EXPECT_EQ(threadCloses, 1);
	}
	TEST_F(FunctionalRunner, FailedStopSignalDoesNotSkipEmergencyCleanup)
	{
		failSet = true;
		const auto result = Run();
		EXPECT_EQ(result.cleanupError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(kills, 1);
		EXPECT_FALSE(running);
	}
	TEST_F(FunctionalRunner, PersistentRestoreFailureIsRetainedAfterRetry)
	{
		restoreFailures = 2;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.restoreError.switchError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(result.restoreRetryError.switchError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(restoreCalls, 2);
		EXPECT_EQ(bound, Original());
	}
	TEST_F(FunctionalRunner, RestoreFailureWithZeroLastErrorDoesNotPass)
	{
		restoreFailures = 1;
		lastError = 0;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		ASSERT_TRUE(result.restoreError.switchError);
		EXPECT_EQ(*result.restoreError.switchError, 0);
	}
	TEST_F(FunctionalRunner, OriginalDesktopFailureDoesNotSwitchRestoreOrLaunch)
	{
		failGet = true;
		const auto result = Run();
		EXPECT_FALSE(result.passed);
		EXPECT_EQ(result.observedError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(restoreCalls, 0);
		EXPECT_FALSE(result.restoreError.switchError);
		EXPECT_FALSE(result.restoreError.threadError);
		EXPECT_FALSE(result.restoreRetryError.switchError);
		EXPECT_FALSE(result.restoreRetryError.threadError);
		EXPECT_EQ(bound, Original());
		EXPECT_TRUE(events.empty());
	}
	TEST_F(FunctionalRunner, EnterFailureStillAttemptsRestorationBeforeReturning)
	{
		failEnter = true;
		const auto result = Run();
		EXPECT_EQ(result.observedError, ERROR_ACCESS_DENIED);
		EXPECT_EQ(restoreCalls, 1);
		EXPECT_EQ(bound, Original());
		EXPECT_EQ(std::ranges::count(events, "create"), 0);
	}
	TEST_F(FunctionalRunner, NoDesktopFixtureMeansNoSwitchOrRestore)
	{
		const auto result = Run(Functional::Scenario::LaunchReady, false);
		EXPECT_TRUE(result.passed);
		EXPECT_EQ(result.evidence, Functional::Evidence::Simulation);
		EXPECT_EQ(Functional::FormatResult(result).find("CURRENT_DESKTOP_INTEGRATION"),
				  std::string::npos);
		EXPECT_EQ(restoreCalls, 0);
		EXPECT_EQ(bound, Original());
	}
	TEST_F(FunctionalRunner, UnknownScenariosAreRejectedAndFormattedSafely)
	{
		for (const auto scenario : { static_cast<Functional::Scenario>(-1),
									static_cast<Functional::Scenario>(3) }) {
			const auto simulated = Run(scenario);
			EXPECT_FALSE(simulated.passed);
			EXPECT_EQ(simulated.observedError, ERROR_INVALID_PARAMETER);
			EXPECT_TRUE(events.empty());
			EXPECT_EQ(bound, Original());
			const auto integration = Functional::RunScenario(scenario, options);
			EXPECT_FALSE(integration.passed);
			EXPECT_EQ(integration.observedError, ERROR_INVALID_PARAMETER);
			EXPECT_EQ(integration.evidence, Functional::Evidence::CurrentDesktopIntegration);
			const auto report = Functional::FormatResult(simulated);
			EXPECT_NE(report.find("SIMULATION\tunknown\tFAIL"), std::string::npos);
		}
	}
	TEST_F(FunctionalRunner, InvalidTimeoutIsRejectedBeforeLaunching)
	{
		options.readyTimeoutMs = INFINITE;
		EXPECT_EQ(Run().observedError, ERROR_INVALID_PARAMETER);
		EXPECT_TRUE(events.empty());
	}
	TEST_F(FunctionalRunner, HookAdapterIsNotInvokedInPhaseFive)
	{
		options.hookTest = +[](DWORD, DWORD, DWORD) -> std::expected<void, DWORD> {
			ADD_FAILURE() << "Real hook checks are disabled";
			return {};
		};
		const auto result = Run();
		EXPECT_EQ(result.observedError, ERROR_NOT_SUPPORTED);
		EXPECT_TRUE(events.empty());
	}
	TEST_F(FunctionalRunner, ReportsSimulationAndExpectedNegativeResultExplicitly)
	{
		readiness = WAIT_TIMEOUT;
		const auto report =
			Functional::FormatResult(Run(Functional::Scenario::ReadinessTimeout));
		EXPECT_NE(report.find("SIMULATION\treadiness_timeout\tPASS"), std::string::npos);
		EXPECT_NE(report.find("expected=1460 observed=1460"), std::string::npos);
		EXPECT_NE(report.find("hook=NOT_RUN"), std::string::npos);
		EXPECT_EQ(report.find("CURRENT_DESKTOP_INTEGRATION"), std::string::npos);
	}
} // namespace DesktopRanger::Tests
