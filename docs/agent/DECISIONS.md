# Architectural decisions

Recorded: 2026-10-22 from the existing local Stable/Research split and [dual-edition architecture](../dual-edition-architecture.md). These entries preserve existing decisions; this session introduces no implementation redesign. Update entries by explicit supersession when a decision changes.

## D001 — Research-First and independent PoCs

Research remains a first-class purpose of the repository. A minimal C++/WinAPI experiment may use its own main/project and Windows SDK only. `check_app_container` and `zis_probe` retain their directories and independence. Future TSF, Clipboard or MXC experiments need not integrate with Core, the launcher or Functional Runner, or modify Stable.

Reason: exploration must remain possible without imposing product infrastructure on an experiment. Research APIs and unusual Windows configurations are allowed; document prerequisites, limitations and the kind of evidence obtained.

## D002 — Two launcher targets in one repository

Stable produces `DesktopRanger.Stable.exe`, accepts no mode arguments and compiles Advanced Desktop only. Research retains `Desktop.Ranger.exe`, its arguments (`--simple`), log and control-window behavior. Both use shared Core/GUI projects and compile the same Advanced implementation sources, including `advanced_desktop_startup.cpp`, with separate entry points and project intermediates. Core/GUI do not reference launchers.

Reason: independent edition builds with minimal extraction and no implementation duplication. No permanent edition branches, separate repositories, plugin layer or edition class hierarchy are required. The split preserves existing Desktop/DACL algorithms and lifetime/recovery ordering.

## D003 — Deliberate promotion into shared Core

Research code moves into Core only when it has a real reusable consumer, a clear contract, regression tests and appropriate functional evidence.

Reason: Core is a reusable dependency, not a mandatory home for exploratory code. Repeatable test probes use explicit protocols, bounded waits, expected outcomes and cleanup; they are distinct from arbitrary PoCs.

## D004 — Independent build graphs and honest evidence

CI uses independent launcher, test and standalone-example jobs with no `needs` dependency between them and matrix fail-fast disabled. A PoC failure may make the workflow red but cannot prevent the Stable build/artifact; branch protection can still require that failed job. Shared Core/GUI failures can affect both editions. Launcher and PoC jobs build without executing them. Local edition builds must run sequentially when sharing Core/GUI intermediates. Supported configurations are Debug/Release x64; ARM64 is future work.

Reason: build independence must coexist with explicit shared dependencies. GoogleTest, simulation and current-Desktop integration do not establish real isolation effectiveness. Stable remains a candidate edition pending VM validation of switching, DACL restoration and recovery; its name does not establish production readiness.

## D005 — Durable session handoff

`AGENTS.md` contains durable working rules, `STATUS.md` contains the mutable session snapshot and evidence, and this file contains architectural decisions and rationale. Each session reads them before work and updates status at handoff. Prior reports must remain distinguishable from newly performed checks. Existing local modifications are preserved; commit, push and PR creation require explicit user permission.

Reason: future sessions must recover context from repository files without relying on chat history or treating an old test result as a fresh validation.
