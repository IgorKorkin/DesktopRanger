# PR 3: Secure Application Launcher in Core

Base: main `549c101` (merged PR 24).
Branch: `feature/extract-secure-application-launcher`.

## Compatibility

| Existing responsibility | Implementation after extraction | Verification |
| --- | --- | --- |
| LaunchAppOnSecureDesktop | Existing application signature delegates to Core LaunchApplication | Core orchestration tests; ControlWindow call sites unchanged |
| LaunchAppAndWaitNewWindow | Existing application signature delegates to Core CreateApplication without ACL changes | ProcessOnlyEntryPointPreservesNoPolicyBehavior |
| Current-user token and SID | Core internal GetUserSidString; token closed on every path | Token open, probe, read, zero-error failure tests |
| Window Station restriction | Existing RestrictedSession and BuildRestrictedDacl | Existing policy tests plus temporary-policy and cleanup-order tests |
| Temporary Desktop access | Same current-user GENERIC_ALL allow DACL | TemporaryPolicyGrantsOnlyCurrentUserGenericAll |
| Desktop restoration | Original DACL snapshot replaces hardcoded D:P restoration | Protected, unprotected, empty, NULL and ACE-order tests |
| CreateProcessW | Shared Core CreateApplication implementation, writable command line, non-inherited handles, SW_SHOW | Fake OS validates parameters and mutates command buffer |
| Process and thread handles | WIL scope guard closes both after callback | Success, callback failure, callback exception and incomplete-information tests |
| Default Desktop branch | Skip ACL changes only when name is Default and handle equals current thread Desktop | Default branch and mismatched-handle tests |
| EXE/LNK resolution, window observation and message pump | Remain in application launcher | Source diff review; real GUI validation remains a VM task |
| Repeated/reentrant launch | App guard protects shared hook state; Core guard protects the permission-changing transaction | Reentrant launch and retry-after-failure tests |

## Lifecycle and errors

The application resolves the executable, clears prior window observation state, installs
WinEventHook, and then calls Core. Core creates the process and invokes the application
wait callback. Both process handles are borrowed by the callback and closed afterwards.
For a protected launch, Desktop is restored before Window Station. The application
removes the hook only after Core returns (also on an exception).

The temporary allow policy lasts throughout the callback, preserving the previous
window-wait interval. Window recognition rules, WaitForInputIdle, the five-second
window wait and message pumping remain in the application. Timeout remains best effort,
as before; it does not terminate the created process.

LaunchError contains an optional primary failure (step and raw WinAPI code), independent
Desktop/Window Station restoration errors, and the PID whenever CreateProcessW succeeded.
A zero raw error code still represents failure when the optional failure is present.
No TerminateProcess behavior is introduced.

Cleanup retains the original snapshot until restoration succeeds and retries a failed
restore. A failed restoration attempt is reported even when a retry succeeds.
RestrictedSession retains its existing final destructor retry if necessary. Desktop
attempts precede all Window Station restoration attempts. Permanent OS failures cannot
be repaired or guaranteed away; the error is returned to the application for logging.
RAII does not run on forced process termination or host shutdown.

## Minimal internal test seam

LaunchApi contains only the OS functions needed for token access, station handles,
process creation, handle cleanup and the existing DaclApi. Public Core entry points
call exactly the same detail implementation exercised by the tests. There is no global
API override or test-only copy of the transaction.

The three existing internal DACL functions accept HANDLE so they can operate on either
Desktop or Window Station (both SE_WINDOW_OBJECT). Public WindowStationPolicy signatures
and its restriction algorithm are unchanged. SecurityDescriptor is reused unchanged.

WIL scope_exit invokes LaunchApi close functions, so fake process/token/station handles
never reach real CloseHandle or CloseWindowStation. Real SID/SD allocations are in-memory
Windows allocations and are released normally with LocalFree.

The small callback keeps GUI waiting out of Core while keeping permissions active until
waiting finishes. CreateApplication is the process-only primitive shared with the
existing public helper that historically launched without applying security policy.
No new resource class, template hierarchy or virtual API wrapper is needed.

## Validation

- Full Release x64 build using scripts/BuildAndTest.ps1 -Configuration Release.
- 103 GoogleTest cases passed: 69 previous and 34 launcher cases; zero failed/skipped.
- New files formatted with project .clang-format; existing C++ formatted on changed ranges.
- clang-format --dry-run --Werror and git diff --check passed.
- No DesktopRanger executable, real Desktop launch or Window Station ACL test run on host.

## VMware validation still required

1. Take a VM snapshot. Record Desktop and WinSta0 DACLs, including protection flags.
2. Launch EXE from the disk picker and application list; repeat with a supported LNK.
   Confirm the hook is installed before CreateProcessW and sees early window events.
3. Test Default and the advanced secure Desktop. Verify visible behavior and timeout
   behavior match the baseline, including applications that create no GUI window.
4. Compare DACLs before and after successful launches and startup failures. Include
   empty/NULL and multi-ACE Desktop DACLs only in the disposable VM.
5. Repeat launches while observing handle counts: process/thread handles must not grow.
6. Trigger nested launch messages during waiting; the nested launch must be rejected,
   followed by a successful subsequent launch.
7. Exercise genuine permission-denied restoration failures and inspect PID plus both
   independent diagnostics. Recovery may require a VM administrator or snapshot restore.
8. Force-terminate DesktopRanger during the temporary-permission interval. Document
   remaining ACLs and manual recovery; forced termination is outside RAII guarantees.

The existing all-process window observation and shortcut argument behavior are retained;
this PR adds no process-tree matching, shortcut redesign, isolation technology or UI change.
Functional validation inside VMware remains the next stage.
