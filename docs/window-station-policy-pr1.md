# PR 1: Window Station DACL lifecycle

Baseline: `be66c04`. No protection mechanisms are added. The protected Desktop
still uses `D:P`, temporarily permits the current user during launch, and returns
to `D:P` afterwards. Only Window Station enumeration rights are restricted.

## Compatibility map

| Baseline function / scenario | Implementation after PR 1 | Verification |
| --- | --- | --- |
| `LaunchAppOnSecureDesktop` | Same public signature and Default-Desktop bypass; snapshot-based `RestrictedSession` for protected Desktop | Core lifecycle unit tests; complete launcher flow awaits VM |
| `LaunchAppAndWaitNewWindow`, `ResolveShortcut`, `GetUserSidString` | Unchanged; executable/shortcut launch and window wait remain | Compilation; EXE/LNK and window timing await VM |
| `RevokeWinsta` / `GrantWinsta` | Signatures retained; named-station session stores original DACL and restores it | Shared session tests; compatibility wrappers await VM |
| `ProcessWindowStation`, `ProcessOnlyWinsta0`, `ProcessAllExistingStations` | Retained, using Core sessions; repeated revoke rejected without losing original snapshot | Shared session tests; enumeration/all-stations scenario awaits VM |
| `IsProcessElevated`, `IsRunningAsSystemNtAuthority` | Unchanged | Compilation; token scenarios await VM |
| `get_error_message`, `PrintAccessRights`, `GetNameSidType`, `PrintSidInfo`, `DumpAcl`, `DumpWindowStationDacl` | Diagnostic functions retained unchanged | Source comparison; runtime diagnostics await VM |
| `OpenStation` | Unchanged; live `READ_CONTROL | WRITE_DAC` handle outlives session | Compilation; opening actual stations awaits VM |
| `SnapshotDacl`, `GetDacl`, `ApplyDacl`, `RestrictDacl`, `RestoreDacl` | Original public signatures retained; OS substitution is internal; restoration includes original DACL protection flag | Public function types checked at compilation; shared production implementation tested through an in-memory OS boundary, exact ACL comparison, NULL/empty distinction, failure propagation |
| `CreateAcl`, `GetAclSizeInformation`, `GetAceAt`, `AppendAce`, `CopyAces`, `CreateEmptyAclLike`, `BuildRestrictedDacl` | Existing ACL transformation retained | Explicit/generic masks, allow/object/callback ACEs, denied ACE, SID/flags/order preservation, source immutability, empty/unsupported ACL cases |

Approved bug corrections: `GrantWinsta` never adds enumeration rights to arbitrary
ACEs. Without a preceding successful revoke it reports `ERROR_NOT_FOUND`.
Repeated revoke reports `ERROR_ALREADY_EXISTS`. A failed restore retains the
snapshot for retry. Launcher failures no longer bypass ACL cleanup. Reentrant or
concurrent launches through the launcher are rejected while one launch is active.
The private legacy `ProcessEnumDesktopMask` algorithm is replaced by the shared
Core implementation; its public calling scenarios above remain available.

## Safe local verification

All new GoogleTest cases replace both `GetSecurityInfo` and `SetSecurityInfo` with
in-memory implementations. They do not open, enumerate or modify any real Desktop
or Window Station. Existing tests remain included.

The internal header `desktop.ranger.core/internal/window_station_policy_internal.h`
holds the two-pointer `DaclApi` and `RestrictedSession`. Public wrappers delegate
to the same internal functions with real WinAPI pointers. Tests substitute only
the two OS calls; ACL conversion, snapshot ownership, retry and RAII cleanup are
the production implementation. No global test override or alternate policy exists.

Launcher cleanup review: argument/SID/descriptor failures occur before any ACL
change; station-open or restriction failure aborts launch. Once restriction
succeeds, the session guard owns restoration. The Desktop guard is armed before
granting temporary access and is destroyed before the station session. Launch
failure, timeout, early return and exception unwinding all traverse these guards.
An explicit Desktop-restore failure triggers its scope-exit retry before the
station guard runs; an explicit station-restore failure retains the snapshot for
the destructor retry. Cleanup logging cannot throw out of either guard.
The atomic entry guard is released after all ACL guards, so protected reentry
from the window message pump cannot replace the original snapshot. Sequential
reentry is allowed after cleanup. This is a source review, not a host integration
test of the launcher; its actual WinAPI order must still be checked in the VM.

```powershell
# Fast incremental build of Core + Tests, then policy tests only:
.\scripts\BuildAndTest.ps1 -Configuration Debug -TestsOnly -TestFilter 'WindowStationPolicy.*'

# Final incremental build of the full solution, then every unit test:
.\scripts\BuildAndTest.ps1 -Configuration Release
```

Logs and GoogleTest XML go into `.output/<configuration>/logs/<timestamp>/`.
Build and test errors, missing XML and zero selected tests yield a nonzero exit
code. The script uses 64-bit MSVC host tools with v145 and never invokes the
launcher or probe executables. No Clean or Rebuild target is used.

## VM-only regression scenarios (not executed on the host)

Use a disposable Windows 11 x64 VM and a checkpoint. Before each test, capture
the complete DACL/control flags of WinSta0 and the test Desktop from a separate
recovery session. Compare ACE order, masks, SID bytes and flags after each test.

1. Run elevated; create/switch Desktop, launch an EXE and an LNK from both UI
   paths, observe its window, Switch Back. Verify original WinSta0 DACL and
   restricted `D:P` Desktop; verify Default-Desktop launch remains unchanged.
2. Use a missing EXE, invalid LNK and an app without a visible window. After
   failure/timeout, verify both ACLs are restored; repeat launch to detect stale
   snapshots. Test non-elevated startup separately.
3. Inject failures opening WinSta0, reading its descriptor, restricting its DACL,
   allowing the Desktop and restoring the Desktop. Verify abort-before-launch
   when restriction fails, Desktop-before-station cleanup and error logging.
4. Inject a transient station-restore error: explicit Restore fails, scope-exit
   retry succeeds. For a permanent failure verify diagnostics and restore using
   the independent recovery session; do not treat the failed cleanup as success.
5. Exercise legacy revoke/grant pairs and all-stations enumeration: exact
   restoration, duplicate revoke, grant without revoke, transient restoration
   failure, and ordinary process exit with an outstanding legacy session.
6. Reenter Launch through the window message pump and attempt concurrent launch;
   verify the second operation is rejected and the first snapshot survives.
7. Force process termination between each ACL transition (including after
   allowing Desktop access). Destructors do not run after `TerminateProcess`,
   a crash or power loss. Confirm the observed residual ACLs; recover with the
   independent pre-captured descriptor or revert the VM checkpoint.

## Limits and regression risks

RAII covers ordinary returns and C++ exception unwinding, not forced termination.
It attempts restoration and reports failures; it cannot guarantee success when
Windows refuses `WRITE_DAC`. Snapshots are process-local and not persisted.
If both station-restore attempts fail, destruction releases the in-memory
snapshot. Do not continue launching applications in that session: recover the
original DACL externally in the VM. A later call cannot reconstruct a lost snapshot.
Restoration can overwrite concurrent ACL edits by another process. Do not mix
legacy sessions and launcher sessions for the same station concurrently.
Real Win32 ACL behavior, app compatibility, window wait timing, UI recovery and
the diagnostic/legacy entry points require the VM runs above; unit tests alone
do not establish end-to-end equivalence with the baseline.
