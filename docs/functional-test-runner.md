# Phase 5: minimal Functional Test Runner

This phase tests runner/process infrastructure on the current Desktop. It does not
test the effectiveness of Desktop isolation. There is no CLI option to create or
switch Desktops, change ACLs, run VMware, or perform cross-process hook experiments.

## Build and run

```powershell
.\scripts\BuildAndTest.ps1 -Configuration Release
.\scripts\RunFunctionalTests.ps1 -Configuration Release
```

Both native projects are in `DesktopRanger.sln`, use MSVC v145, and write to
`.output\Release`: `functional_test_runner.exe` and the precompiled `test_probe.exe`.
The integration script requires both binaries, applies a 60-second outer watchdog,
and saves `functional-results.txt`, stdout and stderr under `.output\Release\functional`.
The script returns nonzero for a failed scenario, missing report, report mismatch or
watchdog expiration. CI runs the same safe integration and uploads the report.

The runner can also be called with `--probe <test_probe.exe>` and `--report <path>`.
The executable name is checked; the probe is never generated or compiled at runtime.

## Three fixed scenarios

| Scenario | PASS criterion | Ready deadline |
| --- | --- | --- |
| `launch_ready` | Explicit ready event, process still alive, graceful cleanup confirmed | 10 seconds |
| `shutdown_cleanup` | Ready, stop request, confirmation that HWND was destroyed, exit code 0 | 10 seconds |
| `readiness_timeout` | `no-ready` probe really times out waiting for ready, then exits cleanly | 250 ms |

Every wait is finite. Graceful shutdown is limited to 1 second (API maximum 2 seconds);
emergency termination to 1 second. The callback's ready deadline begins before process
creation, so a slow CreateProcess call does not reset the budget. CreateProcess itself
is synchronous; the script's outer watchdog bounds a stalled runner. Only the process
created by that scenario can be terminated. The probe independently exits after 15 seconds
in its loop, including when the runner disappears. Immediate cleanup after an arbitrary
runner/OS crash is not guaranteed.

## Readiness and cleanup protocol

Three fresh, manually reset named events are created for each case: ready, stop and
stopped. Names use the existing Core name generator; an already-existing event is
rejected, so stale readiness is not reused. Each event is owned by a WIL handle.

`test_probe.exe` opens those events, creates its own window, paints it and posts a
private message to that window. Only dispatching that message in the message loop
signals ready. Successful CreateProcess or WaitForInputIdle is not readiness; the
runner does not use WaitForInputIdle. It waits on `[process, ready]`, with process
exit taking priority, and checks that the process is still alive after readiness.

Stop is acknowledged by a different event, only after DestroyWindow succeeds on the
window's owning thread. The runner also waits for process exit and checks exit code 0.
Core `SecureApplicationLauncher::CreateApplication` owns and closes both process
handles; the runner's callback only borrows them and completes cleanup before returning.
No launch, DACL or ownership implementation is duplicated in Core.

## Evidence and regression tests

- `SIMULATION`: GoogleTest invokes the same production runner functions, the real Core
  CreateApplication implementation with substituted process APIs, and the real Core
  DesktopLifecycle implementation with fake HDESKs and substituted WinAPI.
- `CURRENT_DESKTOP_INTEGRATION`: real precompiled probe, real HWND/event/process
  protocol, current Desktop only. This is not evidence for isolation effectiveness.
- Real isolation/VM functional trials and SetWindowsHookEx: `NOT_RUN`.

The internal Desktop fixture supplies borrowed fake handles. Restoration runs after
process cleanup on successful and failing paths, retries once, and preserves first and
retry errors independently from the scenario error. Fake HDESKs are never adopted by a
real WIL CloseDesktop deleter. The public integration entry point has no Desktop fixture.

Each row reports PASS/FAIL, expected and observed error, cleanup and termination errors,
switch rollback and both restoration errors, PID, readiness/stopped confirmation and
elapsed milliseconds. A negative case passes only for an actual ready-wait timeout
with successful cleanup; a CreateProcess failure with the same code cannot pass it.

`Options::hookTest` defines a minimal future adapter receiving process ID, GUI thread ID
and a time limit. Phase 5 rejects a non-null adapter with ERROR_NOT_SUPPORTED before
launching anything. No hook is installed and the report always states `hook=NOT_RUN`.

## Deferred real trials

Use a separate, snapshotted Windows VM in a later phase to connect and validate:

1. Secure Desktop creation, switching, restoration of the visible Desktop and the
   worker binding, including partial failures and failed retries.
2. Temporary Desktop/Window Station DACL changes through the existing launcher,
   preserving and restoring original ACL state after normal and failing runs.
3. Cross-process SetWindowsHookEx through the reserved adapter, with explicit expected
   success/denial and cleanup criteria. No protection claim is made in this phase.
4. Runner/probe crashes while on a protected Desktop; recovery and GUI ownership.

TOML, a scenario engine, plugins and a generalized test framework are intentionally
absent: the runner executes the three cases directly and sequentially.
