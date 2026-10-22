#include "../desktop.ranger.core/desktop_name_generator.h"
#include "internal/functional_runner_internal.h"

#include <format>
#include <random>
#include <vector>

namespace DesktopRanger::Functional
{
	namespace
	{
		DWORD LastError()
		{
			const auto error = ::GetLastError();
			return error ? error : ERROR_GEN_FAILURE;
		}
		DWORD Remaining(ULONGLONG deadline, const detail::Api &api)
		{
			const auto now = api.now();
			return now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
		}
		DWORD WaitError(DWORD wait)
		{
			return wait == WAIT_FAILED ? LastError() : ERROR_TIMEOUT;
		}
		bool Valid(const Options &options)
		{
			return !options.probePath.empty() && !options.desktopName.empty() &&
				   options.probePath.find_first_of(L"\"\r\n") == std::wstring::npos &&
				   options.probePath.find(L'\0') == std::wstring::npos &&
				   options.readyTimeoutMs > 0 && options.readyTimeoutMs <= 10000 &&
				   options.shutdownTimeoutMs > 0 && options.shutdownTimeoutMs <= 2000 &&
				   options.terminateTimeoutMs > 0 && options.terminateTimeoutMs <= 1000;
		}
		Result Initial(Scenario scenario, Evidence evidence)
		{
			return { .scenario = scenario,
					 .evidence = evidence,
					 .expectedError = scenario == Scenario::ReadinessTimeout
										  ? DWORD{ ERROR_TIMEOUT }
										  : DWORD{} };
		}
		void StopProbe(HANDLE process, const detail::Signals &signals,
					   const Options &options, Result &result,
					   const detail::Api &api) noexcept
		{
			// Handles are borrowed from Core's callback and must remain open throughout.
			auto wait = api.waitOne(process, 0);
			if (wait == WAIT_OBJECT_0) {
				if (!result.stoppedConfirmed && !result.cleanupError) {
					result.cleanupError = ERROR_PROCESS_ABORTED;
				}
				return;
			}
			if (wait == WAIT_FAILED) {
				result.cleanupError = LastError();
			}
			if (!api.setEvent(signals.stop)) {
				result.cleanupError = LastError();
			}
			wait = api.waitOne(process, options.shutdownTimeoutMs);
			if (wait == WAIT_OBJECT_0) {
				DWORD code{};
				if (!api.exitCode(process, &code)) {
					result.cleanupError = LastError();
				} else if (code != ERROR_SUCCESS) {
					result.cleanupError = code;
				} else if (api.waitOne(signals.stopped, 0) != WAIT_OBJECT_0) {
					result.cleanupError = ERROR_INVALID_STATE;
				} else {
					result.stoppedConfirmed = true;
				}
				return;
			}
			if (!result.cleanupError) {
				result.cleanupError = WaitError(wait);
			}
			// Emergency cleanup is limited to the process just created by this scenario.
			if (!api.terminate(process, ERROR_TIMEOUT)) {
				result.terminationError = LastError();
			} else if (const auto ended =
						   api.waitOne(process, options.terminateTimeoutMs);
					   ended != WAIT_OBJECT_0) {
				result.terminationError = WaitError(ended);
			}
		}

		std::expected<wil::unique_handle, DWORD> CreateSignal(const std::wstring &name)
		{
			wil::unique_handle event{ ::CreateEventW(nullptr, TRUE, FALSE,
													 name.c_str()) };
			const auto error = ::GetLastError();
			if (!event) {
				return std::unexpected(error ? error : ERROR_GEN_FAILURE);
			}
			if (error == ERROR_ALREADY_EXISTS) {
				return std::unexpected(error); // Never accept an old ready signal.
			}
			return event;
		}
	} // namespace

	Result detail::RunScenario(Scenario scenario, const Options &options,
							   const Signals &signals, Evidence evidence, const Api &api,
							   const SimulatedDesktop *desktop)
	{
		Result result = Initial(scenario, evidence);
		const auto started = api.now();
		auto operation = [&] {
			if (!Valid(options) || !signals.ready || !signals.stop || !signals.stopped) {
				result.observedError = ERROR_INVALID_PARAMETER;
				return;
			}
			if (options.hookTest) {
				result.observedError = ERROR_NOT_SUPPORTED;
				return;
			}
			HDESK original{};
			auto restore = wil::scope_exit([&]() noexcept {
				if (desktop) {
					const auto restored = DesktopLifecycle::detail::RestoreDesktop(
						desktop->visibleOriginal, original, desktop->api);
					if (!restored) {
						result.restoreError = restored.error();
						const auto retry = DesktopLifecycle::detail::RestoreDesktop(
							desktop->visibleOriginal, original, desktop->api);
						if (!retry) {
							result.restoreRetryError = retry.error();
						}
					}
				}
			});
			if (desktop) {
				const auto current =
					DesktopLifecycle::detail::GetCurrentDesktop(desktop->api);
				if (!current) {
					result.observedError =
						current.error() ? current.error() : ERROR_GEN_FAILURE;
					return;
				}
				original = current.value();
				const auto switched = DesktopLifecycle::detail::SwitchToDesktop(
					desktop->target, desktop->api);
				if (!switched) {
					result.switchRollbackError = switched.error().rollbackError;
					result.observedError = switched.error().error ? switched.error().error
																  : ERROR_GEN_FAILURE;
					return;
				}
				original = switched.value();
			}
			const auto command = std::format(
				L"\"{}\" \"{}\" \"{}\" \"{}\" {}", options.probePath, signals.readyName,
				signals.stopName, signals.stoppedName,
				scenario == Scenario::ReadinessTimeout ? L"no-ready" : L"normal");
			auto callback =
				[&](const PROCESS_INFORMATION &process) -> std::expected<void, DWORD> {
				result.processId = process.dwProcessId;
				auto stop = wil::scope_exit([&]() noexcept {
					StopProbe(process.hProcess, signals, options, result, api);
				});
				const HANDLE handles[]{ process.hProcess, signals.ready };
				const auto remaining = Remaining(started + options.readyTimeoutMs, api);
				if (!remaining) {
					result.readinessTimedOut = true;
					return std::unexpected(ERROR_TIMEOUT);
				}
				const auto wait = api.waitMany(2, handles, FALSE, remaining);
				// Exit wins over readiness if both handles are signaled.
				if (wait != WAIT_OBJECT_0 + 1) {
					result.readinessTimedOut = wait == WAIT_TIMEOUT;
					return std::unexpected(wait == WAIT_OBJECT_0 ? ERROR_PROCESS_ABORTED
																 : WaitError(wait));
				}
				if (const auto alive = api.waitOne(process.hProcess, 0);
					alive != WAIT_TIMEOUT) {
					return std::unexpected(alive == WAIT_FAILED ? LastError()
																: ERROR_PROCESS_ABORTED);
				}
				result.readyConfirmed = true;
				if (scenario == Scenario::ReadinessTimeout) {
					return std::unexpected(
						ERROR_INVALID_STATE); // no-ready must never signal.
				}
				if (scenario == Scenario::ShutdownCleanup) {
					StopProbe(process.hProcess, signals, options, result, api);
					stop.release();
					if (!result.stoppedConfirmed) {
						return std::unexpected(result.cleanupError ? result.cleanupError
																   : ERROR_INVALID_STATE);
					}
				}
				return {};
			};
			const auto launched = api.launch(options.desktopName, command, callback);
			if (!launched) {
				result.processId = launched.error().processId;
				result.observedError = launched.error().failure
										   ? launched.error().failure->error
										   : ERROR_GEN_FAILURE;
				if (!result.observedError)
					result.observedError = ERROR_GEN_FAILURE;
			} else {
				result.processId = launched.value();
			}
		};
		try {
			operation();
		} catch (const std::bad_alloc &) {
			result.observedError = ERROR_NOT_ENOUGH_MEMORY;
		} catch (...) {
			result.observedError = ERROR_UNHANDLED_EXCEPTION;
		}
		result.elapsedMs = api.now() - started;
		result.passed = result.observedError == result.expectedError &&
						(scenario == Scenario::ReadinessTimeout
							 ? result.readinessTimedOut && !result.readyConfirmed
							 : result.readyConfirmed) &&
						result.stoppedConfirmed && !result.cleanupError &&
						!result.terminationError && !result.restoreError.switchError &&
						!result.restoreError.threadError &&
						!result.restoreRetryError.switchError &&
						!result.restoreRetryError.threadError;
		return result;
	}

	Result RunScenario(Scenario scenario, const Options &options)
	try {
		Result failure = Initial(scenario, Evidence::CurrentDesktopIntegration);
		if (!Valid(options) || options.hookTest) {
			failure.observedError =
				options.hookTest ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
			return failure;
		}
		const auto current = CurrentDesktopName();
		if (!current || *current != options.desktopName) {
			failure.observedError =
				current ? ERROR_INVALID_PARAMETER
						: (current.error() ? current.error() : ERROR_GEN_FAILURE);
			return failure;
		}
		std::mt19937 generator(std::random_device{}());
		const auto prefix =
			L"Local\\DesktopRanger.Test." + DesktopName::Generate(generator, 24);
		const auto readyName = prefix + L".ready";
		const auto stopName = prefix + L".stop";
		const auto stoppedName = prefix + L".stopped";
		auto ready = CreateSignal(readyName);
		if (!ready) {
			failure.observedError = ready.error();
			return failure;
		}
		auto stop = CreateSignal(stopName);
		if (!stop) {
			failure.observedError = stop.error();
			return failure;
		}
		auto stopped = CreateSignal(stoppedName);
		if (!stopped) {
			failure.observedError = stopped.error();
			return failure;
		}
		const detail::Signals signals{ ready->get(), stop->get(), stopped->get(),
									   readyName,	 stopName,	  stoppedName };
		return detail::RunScenario(scenario, options, signals,
								   Evidence::CurrentDesktopIntegration, {});
	} catch (const std::bad_alloc &) {
		auto result = Initial(scenario, Evidence::CurrentDesktopIntegration);
		result.observedError = ERROR_NOT_ENOUGH_MEMORY;
		return result;
	} catch (...) {
		auto result = Initial(scenario, Evidence::CurrentDesktopIntegration);
		result.observedError = ERROR_UNHANDLED_EXCEPTION;
		return result;
	}

	std::expected<std::wstring, DWORD> CurrentDesktopName()
	{
		const auto current = DesktopLifecycle::GetCurrentDesktop();
		if (!current)
			return std::unexpected(current.error());
		DWORD bytes{};
		::GetUserObjectInformationW(current.value(), UOI_NAME, nullptr, 0, &bytes);
		if (bytes < sizeof(wchar_t))
			return std::unexpected(LastError());
		std::vector<wchar_t> name(bytes / sizeof(wchar_t));
		if (!::GetUserObjectInformationW(current.value(), UOI_NAME, name.data(), bytes,
										 &bytes)) {
			return std::unexpected(LastError());
		}
		return std::wstring(name.data());
	}

	std::string FormatResult(const Result &result)
	{
		const char *names[]{ "launch_ready", "shutdown_cleanup", "readiness_timeout" };
		auto code = [](const std::optional<DWORD> &error) {
			return error ? std::to_string(*error) : std::string("none");
		};
		return std::format(
			"{}\t{}\t{}\texpected={} observed={} cleanup={} termination={} "
			"rollback={} restore_visible={} restore_thread={} retry_visible={} "
			"retry_thread={} "
			"pid={} ready={} ready_timeout={} stopped={} elapsed_ms={} hook=NOT_RUN",
			result.evidence == Evidence::Simulation ? "SIMULATION"
													: "CURRENT_DESKTOP_INTEGRATION",
			names[static_cast<int>(result.scenario)], result.passed ? "PASS" : "FAIL",
			result.expectedError, result.observedError, result.cleanupError,
			result.terminationError, code(result.switchRollbackError),
			code(result.restoreError.switchError), code(result.restoreError.threadError),
			code(result.restoreRetryError.switchError),
			code(result.restoreRetryError.threadError), result.processId,
			result.readyConfirmed, result.readinessTimedOut, result.stoppedConfirmed,
			result.elapsedMs);
	}
} // namespace DesktopRanger::Functional
