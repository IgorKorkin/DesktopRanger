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

## KCon file probe integration — 2026-10-23 (current session)

- Checkout: main, HEAD 3b975b5e044683b55b9af47a8a278b01192fee6c. Inherited uncommitted Classic launcher changes and filesystem scripts were preserved; nothing staged or committed.
- Moved the single file probe to zis_probe/file_probe.cpp, exported RunFileProbe and compiled it in the existing zis_probe project. No-argument GUI body is unchanged; the new --file-test dispatch selects the file experiment.
- Classic launcher and fixture/ACL scripts now use bin/zis_probe.exe. CPIS/PSEC implementations, Stable, Core and test_probe were not changed.
- Fresh Release x64 builds: zis_probe and check_app_container both exit 0. Logs: .output/KCon/classic-stage/{zis_probe,check_app_container}-integrated-build.log.
- Fresh host baseline: all four operations ALLOWED, exit 0; Verify-Fixture validated CSV and canary contents. Evidence: .output/KCon/classic-stage/zis-baseline.log. git diff --check passed.
- Status checks ignore submodules because GoogleTest metadata is inaccessible; submodules were not repaired. No GUI/isolated Desktop runtime check was performed. Next: prepare a fresh Classic fixture, grant the existing profile SID and run --classic --file-test ROOT manually in a disposable Windows VM.

## KCon CPIS/PSEC filesystem — итоговый отчёт, 2026-10-24

Рабочий checkout: `C:\Users\igork\public\DesktopRanger`, branch `main`, HEAD
`3b975b5e044683b55b9af47a8a278b01192fee6c`. Все изменения локальные.
Commit, push, pull, reset, PR и staging не выполнялись. Наследованные изменения
Classic/file-probe, project integration, filesystem scripts и этот STATUS сохранены;
унаследованный `KCon-review.diff` не изменялся. Полный status блокируется доступом к
метаданным `external/googletest`; status/diff проверены с `--ignore-submodules=all`.
Stable, Core, test_probe, research-index и тела существующих GUI-сценариев не менялись.

### Реализовано

- `check_app_container.exe --experimental --file-test ROOT`: существующий
  `Experimental_CreateProcessInSandbox`, бинарный SBOX 0.1.0, `app_container=true`,
  `fs_read_only=[ROOT\bin]`, `fs_read_write=[ROOT\sources]`,
  `fs_deny=[ROOT\secrets]`. Используется отдельная фиксированная identity
  `IgorKorkin.ZIS.CPIS.FileTest`; Classic identity не используется в файловой ветке CPIS.
- `check_app_container.exe --psec --file-test ROOT`: существующий
  `CreateProcessSecurityEnvironment` и `CreateProcessW` с
  `PROC_THREAD_ATTRIBUTE_SECURITY_ENVIRONMENT`. PSEC 1.1 содержит те же пути.
  Перед созданием проверяются native FS_DENY и доступная версия контракта.
  Если version-query export отсутствует, принятие версии проверяется самим Create;
  версия не понижается. HANDLE PSEC, DLL и specification buffer сохраняются до выхода
  дочернего процесса. Успешный Create PSEC и успешный CreateProcess печатаются отдельно.
- `filesystem/FilePolicy.h` — небольшой сборщик FlatBuffer без новой зависимости,
  EXE или проекта. Слоты deprecated-поля SBOX сохранены; PSEC SchemaVersion — inline
  struct `{uint16 major=1, uint16 minor=1}`. Реальный передаваемый буфер сохраняется в
  `ROOT\cpis-files.bin` / `ROOT\psec-files.bin`.
- `Prepare-Fixture.ps1` копирует только существующий `zis_probe.exe` в `ROOT\bin`,
  выдаёт fixture только own-user/SYSTEM DACL и Low MIC. Classic package ACE не выдаются.
  Произвольное фиктивное имя `ZIS.FileTest.<GUID>` удалено; manifest указывает backend
  и фактическую launcher identity. JSON — описание, а не входной binary specification.
- `Grant-AppContainerAccess.ps1` использует только проверяемые `icacls /grant`:
  ROOT=RX без наследования, bin=наследуемый RX, sources=наследуемый Modify.
  SACL/MIC не переписываются. SID выводится из `IgorKorkin.ZIS.PoC`; переданный
  несовпадающий SID отклоняется. Скрипт отказывается выдавать Classic ACE на fixture
  с manifest CPIS/PSEC. Архитектура рабочего Classic launcher сохранена.
- Существующий `zis_probe` выполняет прежние четыре Win32-операции и сохраняет
  CSV с исходными кодами ошибок. Добавлено сохранение фактических Desktop, IL,
  TokenIsAppContainer и package SID в `sources\file-probe-context.txt`.
  Файловый baseline может понизить собственный токен до Low (`--low-integrity`).
  Это не меняет no-argument GUI probe.
- `Run-FilesystemExperiment.ps1` создаёт отдельную свежую fixture для каждого
  backend и каждого контрольного случая. Перед политикой на той же fixture запускает
  обычный Low-IL baseline: четыре ALLOWED(0), TokenIsAppContainer=0. Затем сохраняет
  baseline CSV/context/изменённые canary и восстанавливает исходные canary.
  CPIS/PSEC запускаются без Classic ACL и без fallback.
- Для CPIS/PSEC имеются три случая: `allow-secrets` (нативный RW для secrets, deny
  отсутствует, ожидаются четыре ALLOWED(0)); `deny-overrides-grant` (secrets одновременно
  RW и fs_deny, ожидаются 0/0/5/5); `restricted` (требуемая итоговая политика, 0/0/5/5).
  Это позволяет обнаружить игнорируемый fs_deny, а не считать omission доказательством.
- В файловых CPIS/PSEC-ветках дочерний процесс создаётся suspended. DACL и MIC семи
  исходных объектов сохраняются до API, при bound policy до ResumeThread и после
  завершения (`security-before.txt`, `security-bound.txt`, `security-after.txt`).
  Runner требует их равенства; сравнивает Desktop, IL и тип токена контрольных запусков.
  Для CPIS проверяется фактический package SID относительно производного SID identity.
  PSEC может иметь разные package SID в разных окружениях; они сохраняются, но равенство
  случайных SID между отдельными PSEC-объектами не требуется.
- В launcher нет Classic fallback, запуска без политики или исправления NTFS ACL при
  отказе native API. CPIS сохраняет GetLastError в decimal/hex; PSEC — исходный HRESULT
  Create и результаты support/version query. Ошибки policy и CreateProcess видны на
  разных стадиях PSEC. CPIS предоставляет один combined policy/process вызов, поэтому
  по одному его отказу нельзя достоверно отделить эти стадии.

### Фактически проверено в этой сессии

- Оба существующих проекта собраны Release x64, MSVC v145, SDK 10.0.26100.0:
  **0 warnings, 0 errors**. Выходы:
  `C:\Users\igork\public\DesktopRanger\.output\Release\check_app_container.exe`,
  `C:\Users\igork\public\DesktopRanger\.output\Release\zis_probe.exe`.
  Первая сборка с HostX86 инструментами дала LNK1101/MSPDB140.DLL;
  `/p:PreferredToolArchitecture=x64` и отдельные intermediates устранили проблему.
  Проекты и установленный toolchain для этого не изменялись.
- SHA256 check_app_container.exe:
  `ED62B893B4AD9C319913D0036EC89639D77D0CF21C925702B46751CD0947674C`.
  SHA256 zis_probe.exe:
  `B429158613FE0E2B26C6B0B23955F724C6ABE3CC9DAD2C393B088A8A8494ADD4`.
- Шесть реально созданных launcher буферов (`--file-test-spec-only`, по три на
  CPIS/PSEC) независимо декодированы Python-проверкой: identifier, version,
  slots/offsets, bounds/alignment, UTF-8 пути с пробелом/кириллицей, RW/RO/deny
  всех трёх случаев соответствуют схеме. Это **не** проверка принятия политики OS.
- PowerShell parser: все четыре filesystem scripts без синтаксических ошибок.
  `git diff --check --ignore-submodules=all` прошёл.
- Read-only `--psec-probe`: CPIS export найден, четыре PSEC exports найдены;
  QuerySupport HRESULT=0, flags=0xF, FS_DENY=yes; version query HRESULT=0,
  available=TRUE, max minor=1. Это результат хоста, не VMware VM.
- Probe на fixture текущей командной сессии дал четыре ALLOWED(0), корректные CSV
  и canary. Измеренный токен этой сессии: **AppContainer=1, Low, Desktop=Default**.
  Поэтому файл журнала `host-ordinary-baseline.log` несмотря на историческое имя
  является только проверкой probe в унаследованной песочнице, **не ordinary-control
  и не доказательством изоляции**. VM runner специально требует обычный токен
  для собственного Low baseline и отклонит такое окружение.
- Реальные policy creation/process launch CPIS/PSEC, изменение fixture ACL/MIC,
  новый Desktop и изоляционные проверки в этой сессии **не запускались**.
  Пользователь выполнит их самостоятельно в локальной VMware Windows 11 VM;
  подключение к VMware не предпринималось.

Журналы и промежуточные результаты находятся в существующем локальном каталоге
`C:\Users\igork\public\DesktopRanger\.output\KCon\filesystem-stage`:
`check_app_container-build-final.log`, `zis_probe-build-final.log`,
`host-api-probe.log`, `host-ordinary-baseline.log`, `wire-verification.log`,
`decoded-policies.json` и шесть `schema-*` fixtures. Они не являются VM evidence.

| Check | Classic | CPIS | PSEC |
| --- | --- | --- | --- |
| API available | Да* | Экспорт найден на хосте | Exports + support/version query на хосте |
| Policy created | Да* | Не проверено | Не проверено |
| Process launched | Да* | Не проверено | Не проверено |
| Sources read/write | ALLOWED(0) / ALLOWED(0)* | Не проверено | Не проверено |
| Secrets read/write denied | DENIED(5) / DENIED(5)* | Не проверено | Не проверено |
| VM verified | Да* | Нет | Нет |

\* Classic — результат ранее успешной VM-проверки, сообщённый пользователем;
в этой сессии не повторялся. Созданный binary buffer не равен созданной OS policy.

### Точные команды повторного запуска в Windows 11 VM

Запускать в обычном интерактивном PowerShell, без elevation, в disposable VM со
snapshot. Checkout должен содержать текущие локальные изменения, существующие WIL
headers, Visual Studio с v145 и Windows SDK 10.0.26100.0. Не требуется сборка solution,
Core, Stable, test_probe или GoogleTest. Откройте PowerShell в корне checkout;
пути вычисляются из `$repo`. Команды не выполняют Git-операций.

```powershell
$repo = $PWD.Path
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found.' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
foreach ($project in @('check_app_container', 'zis_probe')) {
    & $msbuild (Join-Path $repo "$project\$project.vcxproj") /m /nr:false /nologo `
        /p:Configuration=Release /p:Platform=x64 /p:PreferredToolArchitecture=x64 `
        "/p:SolutionDir=$repo\" "/p:OutDir=$repo\.output\Release\" `
        "/p:IntDir=$repo\.intermediate\$project\KCon-filesystem\"
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $project, exit=$LASTEXITCODE" }
}

$launcher = Join-Path $repo '.output\Release\check_app_container.exe'
$probe = Join-Path $repo '.output\Release\zis_probe.exe'
$runner = Join-Path $repo 'check_app_container\filesystem\Run-FilesystemExperiment.ps1'
$run = Join-Path $env:LOCALAPPDATA ('KCon-Filesystem-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
& $launcher --psec-probe 2>&1 | Tee-Object -FilePath (Join-Path $run 'api-probe.log')
# Inspect the actual export/query results; probe exit 0 alone is not policy proof.
$experimentResults = @()
foreach ($mechanism in @('CPIS', 'PSEC')) {
    $passed = $false
    $failure = $null
    try {
        & $runner -Mechanism $mechanism -LauncherExe $launcher -ProbeExe $probe `
            -EvidenceRoot (Join-Path $run $mechanism)
        if (-not $?) { throw "$mechanism runner reported failure." }
        $passed = $true
    } catch {
        $failure = $_.Exception.Message
        Write-Warning "$mechanism incomplete: $failure"
    }
    $experimentResults += [pscustomobject]@{ Mechanism = $mechanism; Passed = $passed; Error = $failure }
}
$experimentResults | ConvertTo-Json -Depth 3 |
    Set-Content -LiteralPath (Join-Path $run 'experiment-wrapper-results.json') -Encoding UTF8
Write-Host "Raw VM evidence: $run"
if (@($experimentResults | Where-Object { -not $_.Passed }).Count) {
    throw 'One or more filesystem experiments failed; inspect experiment-wrapper-results.json and raw evidence.'
}
```

Runner вызывает ровно новые файловые ветки существующего EXE:
`--experimental --file-test ROOT` и `--psec --file-test ROOT`.
Для автоматической фиксации результата добавляется `--file-test-no-ui`: дочерний
процесс остаётся на `DR_ZIS_POC`, но MessageBox и переключение input Desktop не нужны.
Два дополнительных запуска меняют только политику secrets через
`--file-test-allow-secrets` / `--file-test-deny-overrides-grant`.
Без этих параметров ожидается исходная таблица 0/0/5/5.
Для видимого ручного запуска уже подготовленной **новой, ещё не использованной**
fixture доступны исходные команды без дополнительных опций:

```powershell
# ROOT должен указывать на отдельную свежую fixture соответствующего механизма.
# Пример интерактивного основного теста; полный причинный контроль выполняет runner выше.
$prepare = Join-Path $repo 'check_app_container\filesystem\Prepare-Fixture.ps1'
$cpisFixture = & $prepare -ProbeExe $probe -Mechanism CPIS
& $launcher --experimental --file-test $cpisFixture.Root
$psecFixture = & $prepare -ProbeExe $probe -Mechanism PSEC
& $launcher --psec --file-test $psecFixture.Root
```

Эти два ручных запуска показывают MessageBox на `DR_ZIS_POC`; после OK launcher
возвращает прежний Desktop. Они сами по себе не заменяют Low/allow/deny control.
При необходимости повторить Classic отдельно, используя его рабочую реализацию:

```powershell
& $runner -Mechanism Classic -LauncherExe $launcher -ProbeExe $probe `
    -EvidenceRoot (Join-Path $run 'Classic')
```

### Как оценивать VM evidence и ограничения

В каждом backend-каталоге `results.json` хранит исходные exit codes, проверки случаев
и ошибки; `experiment.json` отдельно показывает итог контроля контекста и независимой
файловой проверки. `VmVerified=null` намеренно не превращается автоматически в true:
факт запуска в VMware, идентификатор VM и build Windows фиксирует экспериментатор.
`windows.json` и SHA256 бинарников сохраняются runner. Любые ошибки оставляют fixtures
и журналы на месте; существующий EvidenceRoot не перезаписывается.

Для положительного результата CPIS/PSEC нужны **все три** случая, каждый со своим
успешным ordinary Low baseline, фактическими CSV Win32 0/0/5/5 (или четырьмя 0 для
allow control), правильными canary, Desktop/IL/token context, и неизменными DACL/MIC
исходных объектов во всех трёх снимках. Фактический backend устанавливается по
launcher API path/log в сочетании с context дочернего процесса; backend CSV label
сам по себе не является аттестацией. Тест проверяет четыре операции canary;
он не доказывает произвольную безопасность всей filesystem/всех прав bin.

SBOX содержит слот fs_deny, однако наличие поля в официальной схеме или успешный CPIS
Create не доказывает поддержку на конкретной Windows build. Если API вернёт ошибку,
сохранить её как результат, а не добавлять ACE или подменять backend. Если API
проигнорирует deny, случай deny-overrides-grant должен выявить доступ к secrets и
провалиться. Если overlap отвергается API, самостоятельная причинная проверка fs_deny
остаётся неподтверждённой, даже если restricted случай дал 0/0/5/5.

Отсутствие FS_DENY/версии PSEC, ошибка Create или расхождение CSV/context/security
означает incomplete/unsupported/failed согласно фактической стадии, не успех.
Если ограничения конкретного backend нельзя применить независимо от NTFS ACL,
нужно явно записать **«независимая от NTFS ACL файловая политика не подтверждена»**.
Данные текущей сессии этого вопроса не решают. Приватная own-user/SYSTEM DACL fixture
сама может ограничивать AppContainer; положительный allow-control и deny-overrides-
grant вместе с обычным Low baseline необходимы, чтобы не выдать это за native deny.
Снимки покрывают исходные canary/каталоги и момент до Resume; они не являются общим
аудитом Windows или доказательством отсутствия любых кратковременных OS изменений.

Следующий шаг принадлежит пользователю: выполнить команды в VMware VM и оценить
сохранённые сырые результаты. После заключительного указания пользователя новые
implementation functions/files не добавлялись; отчёт и инструкции записаны в этот
существующий STATUS.md. Автоматизация подключения к VMware не создавалась.

## KCon: два минимальных диагностических запуска — 2026-10-24

Эта запись уточняет предыдущий план: расширение экспериментов остановлено по запросу
пользователя. Новая инфраструктура, сценарные скрипты и исходные файлы не создавались.
Новые VM-факты от пользователя: Classic PASS 4/4; CPIS GUI без файловых полей работает;
CPIS filesystem allow-secrets создаёт процесс, но тот выходит с 3221225794/0xC0000142
до CSV; последующие deny-overrides-grant/restricted получают API error 183 до создания
процесса. PSEC минимальный GUI также выходит с 0xC0000142. Эти результаты не получены
агентом и не доказывают результат двух новых проверок ниже.

Минимальный diff этой итерации в существующем `check_app_container/main.cpp`:
34 добавленных / 14 удалённых строк. Добавлен один opt-in ключ
`--no-filesystem-policy`. CPIS filesystem получает прежний минимальный SBOX без
filesystem fields; image, identity, CWD, Desktop, probe arguments и suspended/resume
не меняются. PSEC GUI получает PSEC 1.1 без filesystem fields; прочие параметры прежние.
Фактический exit code печатается decimal/hex и возвращается диагностическим PSEC
запуском. Без ключа прежнее поведение сохранено. Desktop, Classic и исходники zis_probe
не переписывались; FilePolicy.h и filesystem scripts в этой итерации не менялись.

Оба существующих проекта собраны Release x64 с PreferredToolArchitecture=x64:
0 warnings, 0 errors. Существующие build-final.log в `.output/KCon/filesystem-stage`
обновлены. `git diff --check --ignore-submodules=all` прошёл; branch/HEAD прежние,
staging/commit/push/pull/reset/PR не выполнялись. Новый launcher SHA256:
`D9B60EAAB819A4A788729CDCB43E71CC7A474650E2AEB05D5CFF283F1C469DEE`.
В VM заменить только launcher и оставить прежний zis_probe.exe, чтобы сохранить
один и тот же probe бинарник. Пересобранный локальный zis_probe не менял исходники.

Две команды в VM (обычная пользовательская сессия). `$Root` — полный путь **той же**
CPIS allow-secrets fixture из предыдущего неуспешного запуска. Не создавать новую
fixture и не запускать многосценарный runner для этих двух проверок:

```powershell
$repo = $PWD.Path # Run from the checkout root.
$launcher = Join-Path $repo '.output\Release\check_app_container.exe'
& $launcher --experimental --file-test $Root --file-test-allow-secrets --file-test-no-ui --no-filesystem-policy
& $launcher --psec --no-filesystem-policy
```

По сравнению с соответствующими прежними командами добавлен только
`--no-filesystem-policy`. Записать API/CreateProcess success либо исходную ошибку и
строку `Probe exit code`. CPIS exit 3 с сообщением о невозможности создать CSV означает,
что probe дошёл до своего кода при отсутствии filesystem grants; это не PASS файловых
операций. PSEC GUI по-прежнему требует закрыть MessageBox, если инициализация удалась.

Identity CPIS остаётся `IgorKorkin.ZIS.CPIS.FileTest`: меняем только файловую политику.
Ошибка 183 рассматривается отдельно как конфликт существующего именованного объекта
при повторном использовании identity; точный объект по одному коду не установлен.
При повторном 183 записать «процесс не создан; контроль инициализации не состоялся».
Профили не удалялись, identity не рандомизируется. Если PSEC без файловых полей вновь
завершится с 0xC0000142, зафиксировать ограничение минимального PSEC GUI на этой VM;
глубокую диагностику DLL/GUI отложить до будущей статьи, без дальнейшего расширения KCon.
Оба новых результата VM на момент этой записи **ожидаются**. Агент их не запускал.

## Постоянные каталоги сборки PoC — 2026-10-24

- В существующих `check_app_container.vcxproj` и `zis_probe.vcxproj` снято условие
  Debug-only с группы OutDir/IntDir. Теперь обе конфигурации используют
  `$(SolutionDir).output\$(Configuration)\` и отдельные
  `$(SolutionDir).intermediate\$(ProjectName)\$(Configuration)\`.
- При отсутствии SolutionDir проекты устанавливают его в родительский каталог
  checkout. Прямая сборка vcxproj теперь использует тот же корень, что Visual Studio
  solution; импорт ThirdParty.props также находится без разового SolutionDir override.
- Оба проекта собраны Release x64 без параметров OutDir, IntDir и SolutionDir:
  0 warnings, 0 errors. Использован Host x64 (`PreferredToolArchitecture=x64`).
  Фактические новые EXE проверены в
  `C:\Users\igork\public\DesktopRanger\.output\Release\check_app_container.exe` и
  `C:\Users\igork\public\DesktopRanger\.output\Release\zis_probe.exe`.
  Свежие журналы — существующие `*-build-final.log` в `.output/KCon/filesystem-stage`.
- MSBuild getProperty подтвердил TargetPath/IntDir для Debug и Release x64;
  Debug не пересобирался. C++-исходники и существующие KCon изменения сохранены.
  Branch main, HEAD 3b975b5e044683b55b9af47a8a278b01192fee6c прежние;
  commit/push/staging не выполнялись. Submodule metadata по-прежнему не исправлялись;
  git diff/status проверяются с `--ignore-submodules=all`.
- Следующий шаг по KCon не изменился: пользователь выполняет два ранее подготовленных
  минимальных контрольных запуска в VM. Эта задача меняет только настройки сборки.

## PR #28: локальные исправления Cubic — 2026-10-25

- Ветка `research/kcon-sandbox-experiments`, HEAD
  `6f013bc7fb872e49f9df2dfc1fbe17a90bda28d2`; исходное дерево чистое при
  `--ignore-submodules=all`. По последнему указанию пользователя commit, staging,
  push и изменение review threads не выполнялись; все исправления оставлены локально.
- Исправлены пять замечаний: Leaf-проверка ProbeExe; отдельные результаты CPIS/PSEC
  с общим throw после обоих запусков; Classic `--file-test-no-ui` в launcher/runner;
  переносимые команды через `$repo`/`$launcher`; переименование filters в
  `zis_probe.vcxproj.filters` с актуальными main.cpp/file_probe.cpp.
- Classic без UI сохраняет SECURITY_CAPABILITIES и `WinSta0\DR_ZIS_POC`, исключая
  MessageBox и захват/переключение input Desktop. Интерактивный путь сохраняется.
  Тела CPIS/PSEC launchers, PSEC builder, SID/Desktop helpers, FilePolicy.h и оба
  исходника zis_probe сравнены с HEAD и не изменились.
- Одна сборка каждого проекта Release x64: 0 warnings, 0 errors. Неинвазивные проверки
  прошли: шесть комбинаций отказов wrapper (включая exit 37), раннее отклонение
  directory/missing/wrong-name ProbeExe без создания fixture или изменения ACL,
  11 CLI-проверок до создания профиля/Desktop, синтаксис PowerShell и filters XML.
  Обычный probe выполнил четыре операции с проверкой CSV, canary и наличия context;
  это проверка reporting в унаследованной сессии, не доказательство sandbox isolation.
- Evidence: `.output/KCon/cubic-review/` (build, script, CLI/probe, compatibility logs).
  `git diff --check --ignore-submodules=all` прошёл. Полный diff/status блокируется
  недоступными метаданными external/googletest; подмодуль не изменялся.
- GUI и sandbox integration: NOT RUN в этой итерации; VM не запускалась.
  По новым данным пользователя GUI Classic/CPIS/PSEC работали на Windows 11 26H2
  build 26300.9711, включая старый и текущий PSEC. На build 28000.3231 обе версии
  PSEC завершались с 0xC0000142. Эти данные уточняют прежние сообщения о VM-сбоях;
  файловое enforcement ими не подтверждается. `OpenDesktop(Default)=ALLOWED` остаётся
  наблюдаемым ограничением; успешный GUI не доказывает полной Desktop isolation.
