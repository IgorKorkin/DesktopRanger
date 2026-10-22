# Session handoff

Updated: 2026-10-22. Repository: `DesktopRanger`.

## Revision and publication

- Branch: `feature/minimal-functional-test-runner`.
- Implementation HEAD at validation, before the documentation-only handoff commit: `b9f676df6a1b3a65a06797889ceb7d66f473ba3e` (`fix: address runner error handling and evidence review findings`). Resolve the current checkout HEAD with `git rev-parse HEAD`; the handoff commit itself follows this implementation revision.
- PR: [#27](https://github.com/IgorKorkin/DesktopRanger/pull/27), open and unmerged when inspected. Confirmed published PR HEAD: `f5199ef253f0513c4d12c04185f5691645e93a15`. This earlier commit contains the Stable/Research split and initial handoff documents; its parent contains the functional runner.
- Review corrections are committed locally. Push was attempted but not completed: native Git Credential Manager could not access credentials (Windows access denied); the connected GitHub integration rejected tree creation with HTTP 403, `Resource not accessible by integration`. No remote ref was changed by the agent.
- Push this branch from an authenticated local shell, then confirm that PR #27 points to the new branch HEAD. Do not mark the fixes published or review complete until verified.
- The user's latest instruction is to skip GitHub replies and manual thread resolution. Neither was performed. Do not merge.

## Review disposition

- FIX P2: missing-report diagnostics now include the runner exit code and log directory; a PowerShell regression test simulates early exit code 37.
- FIX P2: Desktop restoration requires a captured original HDESK. Regression coverage verifies no switching, restoration or probe launch after capture failure and preserves recovery after entry failure.
- FIX P2: the internal API-injection entry point always reports simulation, including substituted APIs without a SimulatedDesktop fixture. Only the public native entry point assigns current-Desktop integration evidence; callers cannot select evidence through the internal signature.
- FIX P3: unknown Scenario values are rejected by both entry points and formatted with a safe `unknown` fallback; negative and out-of-range values are tested.
- FIX P3: shared launcher startup catches `std::filesystem::filesystem_error` from StatusLogger::Init and follows the existing failure-message/EXIT_FAILURE path. Both launcher builds cover compilation; this exception path was not fault-injected at runtime.
- FIX P3: documentation now states directly that a required failed PoC check blocks merge, and this handoff uses only the repository name instead of a machine-specific account/path.
- IGNORE P3: retain small vswhere/MSBuild lookup duplication. A composite action or reusable workflow adds infrastructure and would still need a local-script entry point; the independent jobs and local script remain straightforward. Reconsider extraction only if lookup behavior becomes more complex.

## Fresh validation

- Stable, Research and functional runner/probe built sequentially in Release x64: each build log reports zero warnings and zero errors.
- GoogleTest: 162/162 passed, zero skipped. Includes 27 FunctionalRunner cases. Targeted FunctionalRunner tests also passed before the full run.
- Current-Desktop integration: 3/3 passed, no Desktop switching or ACL changes. Real isolation and hook checks were not run.
- `scripts/tests/RunFunctionalTests.Tests.ps1`: passed missing-report diagnostics regression. It uses a fake process and does not execute the runner.
- Evidence, relative to the repository: `.output/Release/logs/pr27-review/stable.log`, `research.log`, `functional.log`, `googletest/{build.log,tests.log,test-results.xml}`, and `integration/{functional-results.txt,runner.log,runner-errors.log}`. Logs are local ignored artifacts.
- `git diff --check` passed. Full `git status --short` succeeded during this review; prior-session submodule access restrictions were not reproduced in that check.

## Boundaries and remaining work

Research-First, independent PoCs, Stable/Research project boundaries, Core DACL algorithms and Desktop lifecycle implementation remain unchanged. See [architecture](../dual-edition-architecture.md), [decisions](DECISIONS.md) and root `AGENTS.md`.

After successful publication, recheck remote HEAD and PR state and update this handoff with the confirmed revision. Review threads remain untouched by instruction; no claim of Cubic approval is made.

Functional validation in a disposable Windows VM with a snapshot is still pending: Advanced entry/exit in both launchers, Research `--simple`, control-window actions, per-monitor borders, DACL preservation/restoration and visible Desktop/worker-binding recovery including failure paths. Stable is not a production-ready or security-validated release.

## Resume procedure

Read root `AGENTS.md`, this file, [DECISIONS.md](DECISIONS.md) and the architecture document. Recheck branch, HEAD, status, both diffs and PR state before editing. Preserve local work and distinguish fresh validation from historical results. Publication is pending the authentication/access block above; merge remains unauthorized.
