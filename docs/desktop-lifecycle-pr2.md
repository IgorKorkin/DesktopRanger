# PR 2: Secure Desktop Lifecycle into Core

Baseline: `9a93be7` (main after PR #23). Work branch:
`feature/complete-desktop-lifecycle`. Changes are reviewed in PR #24.

## Scope and compatibility

| Existing entry point / behavior | Implementation | Verification |
| --- | --- | --- |
| `AdvancedSecureDesktop::CreateNewDesktop` | Thin adapter to Core; retains the raw caller-owned output and borrowed original handle | Shared Core factory tests; actual adapter/Windows creation requires VM |
| `AdvancedSecureDesktop::SwitchToDesktop` | Thin adapter to Core with both primary and rollback diagnostics | Core negative-path tests; wrapper compiled |
| `GetDesktopHandle`, `GetDesktopName` | Borrowed active-session state remains in application; cleared after worker joins and before owned handle closes | Lifetime source review; ControlWindow scenarios require VM |
| `GetRandomDesktopName` | Existing Core generator unchanged | All existing generator tests retained |
| Advanced worker / `StartAdvancedDesktop` | Core switch/restore, worker-owned HWND cleanup, joined `jthread`, WIL-owned Desktop | Core unit tests and lifetime source review; threading and HWND behavior require VM |
| `ControlWindow::ShowContolWindow` | Existing void entry point retained; new `RunControlWindow` reports errors to worker and destroys HWND in its creating thread | Compilation and source review; GUI requires VM |
| `main` | Returns Advanced worker success/failure rather than unconditional success | Compilation/source review; process exit scenarios require VM |
| Simple Desktop, launcher, WindowStationPolicy, diagnostic projects | No functional extraction in this PR | Existing 34 tests retained; formatting only where applicable |

The model is unchanged: a non-inheritable Desktop is created with `D:P` and an
owner handle requesting `GENERIC_ALL`. Current/Desktop-name state remains usable
by ControlWindow throughout its protected session. No new isolation mechanism or
process-launch behavior is added.

## Minimal Core API

`desktop.ranger.core/desktop_lifecycle.h` contains:

- `UniqueDesktop`: `wil::unique_any` calling the real `CloseDesktop` in production.
- `GetCurrentDesktop()`: returns a borrowed handle.
- `CreateSecureDesktop(name)`: returns the owned Desktop or a Win32 error.
- `SwitchToDesktop(target)`: binds/verifies/switches; returns the borrowed prior
  Desktop, or `SwitchError { step, error, rollbackError }`.
- `RestoreDesktop(original)`: attempts visible restoration first and then thread
  rebinding, independently; returns both `switchError` and `threadError` on failure.

The internal header exposes only the four OS function pointers needed by this
module. Production and tests use the same creation, verification, switching and
rollback implementation. The small internal ownership factory is instantiated
with the production WIL owner or a WIL owner whose deleter is the test callback.
There is no virtual hierarchy, global override, or parallel test policy.

## Safe test boundary

All 35 lifecycle tests explicitly supply fake GetThreadDesktop/CreateDesktopW/
SetThreadDesktop/SwitchDesktop callbacks. Every owned fake HDESK uses `TestOwner`,
whose compile-time deleter is the test `Close` callback. No fake handle is ever
adopted by production `UniqueDesktop`, and the public creation function is only
inspected by a compile-time type assertion in these tests.

Tests cover D:P/noninheritance/access masks, name termination, creation failure,
borrowed handles, move/release/scope-exit/exception ownership, initial binding
failure, post-binding verification failure, visible switching failure, independent
rollback failure, restoration ordering, both restoration errors and retry after a
transient failure. A successful rollback means the API accepted the attempt; it
does not promise recovery after a later OS or process failure.

## Lifetime and remaining VM checks

ControlWindow destroys its top-level HWND (and therefore children) before returning.
The worker's border guard runs next, then the Core restore operation. Cleanup
and restore errors are logged; worker state is inspected after join and produces
the process exit status. Application Desktop state is cleared before the owner
closes HDESK. A worker-creation exception cannot release HDESK while a worker runs:
the `jthread` scope joins before the Desktop owner scope exits.

Run only in a disposable Windows 11 VMware VM with a checkpoint:

1. Elevated create/switch, border, app list, EXE/LNK launch and Switch Back. Check
   unchanged UI, valid ControlWindow borrowed handle and final Desktop cleanup.
2. Repeat the session; inspect for leaked HWND/HDESK/thread resources and stale
   application state. Verify the Simple Desktop scenario remains unchanged.
3. Inject creation and thread-start failures; confirm no double close or close
   of the borrowed original handle and a failing process exit status.
4. Inject initial SetThreadDesktop, verification, SwitchDesktop and rollback
   failures separately and together. Preserve distinct error diagnostics.
5. Inject control-window/border creation, GetMessage and DestroyWindow failures.
   Check owner-thread HWND cleanup, independent visible restoration and thread
   rebinding, and propagation of worker/cleanup/restoration errors.
6. Leave a thread hook/window deliberately active to provoke ERROR_BUSY on
   rebinding. Verify this is reported as failed recovery, not a guaranteed rollback.
7. Force process termination during a session. RAII does not run after every
   crash/TerminateProcess; use the independent recovery desktop or VM checkpoint.

Unit tests do not establish actual CloseDesktop success, GUI cleanup, OS Desktop
switching, or app compatibility. WIL destruction does not return CloseDesktop's
BOOL to the caller. These are explicit VM validation boundaries.

## Formatting and local build

At the user's request clang-format 22.1.8 was applied to all 32 repository-owned
C++ files using the unchanged root `.clang-format`. Third-party submodules and
generated output were excluded. Existing non-lifecycle changes are formatting;
the Windows/Sddl include groups in the older policy test also preserve the SDK's
required inclusion order after sorting.

Fast iteration: `scripts/BuildAndTest.ps1 -Configuration Debug -TestsOnly
-TestFilter 'DesktopLifecycle.*'`. Final validation: `scripts/BuildAndTest.ps1
-Configuration Release`. Logs/XML remain under `.output/` and are not source files.
