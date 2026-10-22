# DesktopRanger: Stable and Research

One repository, shared sources, two independently buildable launcher projects.
Stable is a target user edition, **not a production-ready or security-validated release**.
Desktop switching, temporary DACL changes and recovery still require functional testing
in a disposable Windows VM with a snapshot. A successful build is not security evidence.

## Boundaries and compatibility

| Component | Responsibility |
| --- | --- |
| DesktopRanger.Stable | Advanced Desktop only; outputs DesktopRanger.Stable.exe |
| DesktopRanger.Research | Existing Desktop.Ranger.exe; Advanced by default, --simple for Simple Desktop |
| Desktop.Ranger.Core | Shared reusable system and security operations |
| Desktop.Ranger.GUI | Shared screen border implementation |
| check_app_container and zis_probe | Independent research executables; no mandatory Core dependency |
| Functional Runner and test_probe | Defined, repeatable test scenarios and explicit readiness protocol |

The existing Research executable name, arguments, logging and control-window behavior
remain unchanged. Stable accepts no mode arguments, uses DesktopRanger.Stable.log and
shares the Advanced Desktop UI. It does not compile Simple Desktop or any research PoC.
The standalone Simple Desktop example remains available.

`desktop.ranger/advanced_desktop_startup.cpp` contains the existing elevation/startup
and Advanced Desktop worker orchestration extracted from main.cpp. Both projects compile
that same file, advanced_secure_desktop.cpp, control_window.cpp, secure_app_launcher.cpp
and status_logger.cpp. No copies, new shared library, plugins or edition class hierarchy
are introduced. Each launcher has its own main.cpp and intermediate directory; Core and
GUI are shared project references. Core/GUI do not reference either launcher.

The Desktop/DACL algorithms, borrowed/owned HDESK lifetime, worker join, window teardown,
green/yellow borders and restoration order are unchanged by the split.

## Research-First

A small C++/WinAPI PoC may have its own main and project with only Windows SDK dependencies.
It need not integrate into Core, the launcher, or the Functional Runner. Existing
check_app_container and zis_probe stay in their current directories. Future TSF, Clipboard,
MXC or other experiments can follow that pattern without modifying Stable.

Experimental APIs and unusual Windows configurations are permitted in Research/PoCs;
document prerequisites, limitations and whether claims are hypotheses, demonstrations,
simulation results or measured functional behavior. Research code is not automatically
promoted into Core. Promotion requires a real reusable consumer, a clear contract,
regression tests and appropriate functional evidence. No permanent edition branches or
separate repositories are required.

Repeatable test applications have an explicit protocol, expected outcomes, bounded waits
and cleanup. They belong with the test infrastructure, not an arbitrary collection of PoCs.
The current runner reports simulations/current-Desktop integration separately from real
isolation tests. No new isolation effectiveness is claimed by this architecture change.

## Independent builds (PowerShell, repository root)

Locate MSBuild with Visual Studio Installer vswhere, then run either project directly:

```powershell
& $msbuild .\desktop.ranger.stable\desktop.ranger.stable.vcxproj /m /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$PWD\"
& $msbuild .\desktop.ranger\desktop.ranger.launcher.vcxproj /m /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$PWD\"
.\scripts\BuildAndTest.ps1 -Configuration Release -TestsOnly
```

These project targets build only the selected launcher and its Core/GUI dependencies.
The solution remains a convenience for building all projects; it is not the isolated Stable
target. CI has independent Stable/Research matrix jobs with fail-fast disabled, a separate
test job, and independent standalone-example jobs. There are no `needs` dependencies
between them. A PoC failure can make the overall workflow red, but cannot cancel or prevent
the Stable build/artifact. Repository branch protection may still require the failed job.
CI builds launchers and PoCs but never executes them. Tests run only mocks and safe
current-Desktop integration. Shared Core/GUI failures can affect both editions by design.

Only Debug/Release x64 are configured. Any CPU aliases are removed; ARM64 remains a future
build-and-test task, independent of edition selection. Outputs stay in .output/Release;
intermediate files are separated by project. Do not build both project graphs concurrently
in the same checkout when they would write the same shared Core/GUI intermediates.

## Functional validation still pending

In a VM with a snapshot: Advanced entry/exit in both executables, Research --simple,
unchanged control-window actions, per-monitor borders, DACL preservation/restoration,
visible Desktop and worker-binding recovery including failure paths. Stable must not be
advertised as production-ready until these checks and their acceptance criteria are met.
