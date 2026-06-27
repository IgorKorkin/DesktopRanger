# DesktopRanger

[![Build DesktopRanger](https://github.com/IgorKorkin/DesktopRanger/actions/workflows/build.yml/badge.svg)](https://github.com/IgorKorkin/DesktopRanger/actions/workflows/build.yml)

**DesktopRanger** is an experimental open-source prototype for hardening Windows Desktop isolation.

The project demonstrates how Windows Desktop and Window Station objects can be used to create a protected environment for sensitive applications and reduce exposure to local user-mode input interception.

Repository:

```text
https://github.com/IgorKorkin/DesktopRanger
```

## Release

Pass the SALT 2026 public prototype release:

```text
https://github.com/IgorKorkin/DesktopRanger/releases/tag/v0.1-pts-2026
```

The release includes source code and a CI-built Windows x64 binary:

```text
DesktopRanger-v0.1-pts-2026-win-x64.zip
```

SHA-256:

```text
3a272b4438314bd38141c338b861602b3d59fe37682d27bd0b66b1acb1af3871
```

## Scope

This public repository contains the defensive DesktopRanger prototype only.

It does not include:

* keylogging tools;
* offensive test harnesses;
* bypass experiments;
* raw experimental logs;
* private research materials.

## Features

DesktopRanger currently demonstrates:

* creation of a separate Windows Desktop;
* access control for Desktop and Window Station objects;
* launch of an application inside the protected desktop;
* switching to and from the protected desktop;
* basic status logging and control flow.

## Build

Requirements:

* Windows 11 x64;
* Visual Studio 2026;
* MSVC Platform Toolset v145;
* Windows SDK;
* Git submodules.

Clone:

```powershell
git clone --recurse-submodules https://github.com/IgorKorkin/DesktopRanger.git
cd DesktopRanger
```

Build Release x64:

```powershell
msbuild .\DesktopRanger.sln /m /p:Configuration=Release /p:Platform=x64
```

Build output:

```text
.output\Release\
```

## Project Layout

```text
desktop.ranger/           main prototype
simple.desktop.console/   simplified console implementation
external/wil/             Microsoft WIL submodule
props/                    Visual Studio property files
.github/workflows/        CI/CD workflows
```

## CI/CD

GitHub Actions builds the project on Windows with Visual Studio 2026.

The release binary is built by CI/CD and attached to the GitHub Release.

## Limitations

DesktopRanger is a research prototype.

It is not production-ready and does not protect against:

* kernel-mode compromise;
* physical access attacks;
* compromised target applications;
* all possible local input-observation techniques.

## Responsible Use

DesktopRanger is intended for defensive research, Windows isolation experiments, and security engineering discussions.

Do not use this project for unauthorized activity, malware development, credential theft, or offensive keylogging.

## License

Apache License 2.0.

## Author

Igor Korkin
Independent Security Researcher

```text
https://igorkorkin.github.io/
```

## Conference Note

This repository accompanies the Pass the SALT 2026 talk on hardening Windows Desktop isolation.

The public release contains the defensive prototype only. Internal evaluation harnesses and keylogging test tools are intentionally not included.
