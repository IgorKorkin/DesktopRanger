# DesktopRanger: session continuity

## Start of every session

1. Read `docs/agent/STATUS.md`, `docs/agent/DECISIONS.md` and `docs/dual-edition-architecture.md`.
2. Check the current branch, HEAD, `git status --short`, `git diff` and `git diff --cached` before editing. Inspect relevant untracked files separately; they do not appear in diff.
3. Preserve existing local work. Distinguish inherited changes from this session's changes. If submodule inspection is blocked, report the limitation; `--ignore-submodules=all` is only a partial check.
4. Follow the user's current task. Pending work in STATUS is context, not authorization to expand scope.

## Architectural boundaries

- Preserve Research-First development: small C++/WinAPI PoCs may have their own main/project and only Windows SDK dependencies. Do not require Core, launcher or Functional Runner integration.
- Keep `check_app_container` and `zis_probe` independent. Experiments such as TSF, Clipboard and MXC must not require Stable changes.
- Stable builds Advanced Desktop only. Research retains `Desktop.Ranger.exe`, Advanced by default and `--simple`, existing logging and control-window behavior.
- Share existing Advanced sources directly between the two launcher projects. Core/GUI must not depend on either launcher. Avoid duplicate implementations and unnecessary edition frameworks.
- Promote research code into Core only with a real reusable consumer, a clear contract, regression tests and appropriate functional evidence.
- Preserve Desktop/DACL behavior, borrowed/owned HDESK lifetime, worker join, window teardown and restoration order unless explicitly required by the task.
- Keep repeatable protocol-driven test probes distinct from exploratory PoCs. Separate simulation, current-Desktop integration and actual isolation evidence.

## Validation and workflow

- Use Windows/MSVC v145 and Debug/Release x64. See the architecture document for independent project build commands and `scripts/BuildAndTest.ps1 -Configuration Release -TestsOnly` for GoogleTest.
- Build Stable and Research sequentially in one checkout: shared Core/GUI intermediates can collide. A solution build also includes other projects and does not prove an isolated Stable build.
- Run checks appropriate to changed behavior. For documentation-only changes, review links, facts and diff; no rebuild is required.
- Desktop switching, DACL mutation and recovery checks belong in a disposable Windows VM with a snapshot. Do not launch those scenarios on the working desktop as a routine check.
- Stable is a candidate user edition, not a production-ready or security-validated release. A successful build or GoogleTest run does not prove isolation effectiveness.
- Do not commit, push or create a PR without explicit user permission. Do not reset, clean or overwrite inherited work.

## End of every session

Update `docs/agent/STATUS.md` with the current branch/HEAD, inherited and new changes, checks actually performed, evidence paths, limitations and the next concrete step. Mark prior-session reports separately from checks repeated now. Record durable architectural decisions and their rationale in `docs/agent/DECISIONS.md`; retain history and explicitly supersede obsolete decisions. Keep this file focused on durable working rules.
