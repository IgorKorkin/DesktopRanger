# PR №4: production startup and dependencies

## Project boundaries

- `Desktop.Ranger.Core` owns process elevation checks and Desktop lifecycle operations.
- `Desktop.Ranger.GUI` owns the screen-border window implementation. The launcher and
  `simple.desktop.console` example both link this project; neither contains a copy of it.
- The main executable starts Advanced Desktop by default. `--simple` runs the existing
  experimental Simple Desktop implementation; the sample's separate `main.cpp` is not
  compiled into the launcher.
- The border is only a visual mode indicator: green for Advanced Desktop, yellow for Simple
  Desktop. It does not detect attacks or provide a security boundary. A red attack-attempt
  indicator may be considered in a later research stage; no attack state or timer exists.
- Each monitor has its own overlay window and a complete rectangular border, including
  shared edges. Monitor bounds come from `EnumDisplayMonitors` and `MONITORINFO.rcMonitor`,
  so different sizes, offsets and negative coordinates do not require special geometry.
- `desktop.ranger` contains the advanced-launcher orchestration and the control-window UI.
- `simple.desktop.console` continues to build its separate example implementation.
- `winstation_patcher.cpp/.h` remain available as historical/experimental sources, but the
  production launcher no longer compiles or calls them. The unreferenced revoke/grant and
  SYSTEM-account diagnostics were not moved into Core.

## Desktop restoration contract

The main thread captures its current Desktop before creating the secure Desktop. The worker
records its own borrowed current-Desktop handle before switching, and `SwitchToDesktop`
returns that borrowed handle on success. Both handles remain
valid while the worker runs because the main thread joins it before releasing the owned
secure `HDESK`. During teardown, worker-created windows are destroyed on the worker first;
then `RestoreDesktop` calls `SwitchDesktop` with the main thread's original Desktop and
`SetThreadDesktop` with the worker's original Desktop. These operations can fail
independently; both error values are retained and logged. Borrowed original handles are
never passed to `CloseDesktop`.

Unit tests use fake `HDESK` values and a substituted Desktop API. They verify selection of
the two original handles, order of restoration, independent errors, and that borrowed
handles are not closed. No real Desktop or UI switching is performed by the unit suite.
The border is created and destroyed by its owning worker thread. Destroying the border does
not post `WM_QUIT`; the control window owns its message loop.

## VMware follow-up

On a disposable Windows VM, verify normal entry/exit from Advanced Secure Desktop,
worker-window teardown before Desktop-handle release, and visible Desktop restoration.
Also exercise failures entering the secure Desktop, creating the border/control windows,
and restoring either the visible Desktop or worker binding. Record both restore errors
when both operations fail. Confirm `--simple` starts the experimental Desktop and paints
a yellow border while Advanced Desktop remains green. These interactive checks are
deliberately not run on the development host.
