param(
    [Parameter(Mandatory = $true)][ValidateSet('Classic', 'CPIS', 'PSEC')][string]$Mechanism,
    [Parameter(Mandatory = $true)][string]$LauncherExe,
    [Parameter(Mandatory = $true)][string]$ProbeExe,
    [Parameter(Mandatory = $true)][string]$EvidenceRoot
)
$ErrorActionPreference = 'Stop'
$launcher = (Resolve-Path -LiteralPath $LauncherExe).ProviderPath
$probe = (Resolve-Path -LiteralPath $ProbeExe).ProviderPath
$evidence = [IO.Path]::GetFullPath($EvidenceRoot)
if (Test-Path -LiteralPath $evidence) { throw 'EvidenceRoot must be fresh; existing evidence is never overwritten.' }
New-Item -ItemType Directory -Path $evidence | Out-Null
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' |
    Select-Object ProductName, DisplayVersion, CurrentBuild, UBR |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'windows.json') -Encoding UTF8
Get-FileHash -Algorithm SHA256 -LiteralPath $launcher, $probe |
    Export-Csv -NoTypeInformation -LiteralPath (Join-Path $evidence 'binaries.csv')
function Invoke-Logged([string]$Exe, [string[]]$Arguments, [string]$Log) {
    # Native stderr is evidence, including expected API failures in Windows PS 5.1.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments 2>&1 | Tee-Object -FilePath $Log | Out-Host
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    return $code
}
$cases = if ($Mechanism -eq 'Classic') { @('restricted') }
         else { @('allow-secrets', 'deny-overrides-grant', 'restricted') }
$results = @()
foreach ($case in $cases) {
    $caseDir = Join-Path $evidence $case
    New-Item -ItemType Directory -Path $caseDir | Out-Null
    $fixture = & (Join-Path $PSScriptRoot 'Prepare-Fixture.ps1') -ProbeExe $probe `
        -Root (Join-Path $caseDir 'fixture') -Mechanism $Mechanism
    $rootPath = $fixture.Root
    $record = [ordered]@{ Mechanism = $Mechanism; Case = $case; Root = $rootPath
        LowBaselineExit = $null; LauncherExit = $null; Verified = $false; Error = $null }
    try {
        # The SAME fixture must permit all four operations at Low IL before any
        # sandbox policy or Classic package ACE is installed.
        $record.LowBaselineExit = Invoke-Logged (Join-Path $rootPath 'bin\zis_probe.exe') `
            @('--file-test', $rootPath, 'baseline', 'low_control', '--low-integrity') `
            (Join-Path $caseDir 'low-baseline.log')
        if ($record.LowBaselineExit -ne 0) { throw "Low baseline failed: exit=$($record.LowBaselineExit)" }
        & (Join-Path $PSScriptRoot 'Verify-Fixture.ps1') -Root $rootPath -Baseline `
            -ExpectedBackend low_control -RequireLowIntegrity -RequireOrdinaryToken
        Move-Item -LiteralPath (Join-Path $rootPath 'sources\file-probe-results.csv') -Destination (Join-Path $caseDir 'low-baseline.csv')
        Move-Item -LiteralPath (Join-Path $rootPath 'sources\file-probe-context.txt') -Destination (Join-Path $caseDir 'low-context.txt')
        [IO.File]::WriteAllText((Join-Path $caseDir 'low-source.after.txt'), [IO.File]::ReadAllText((Join-Path $rootPath 'sources\main.cpp')))
        [IO.File]::WriteAllText((Join-Path $caseDir 'low-secret.after.txt'), [IO.File]::ReadAllText((Join-Path $rootPath 'secrets\token.txt')))
        [IO.File]::WriteAllText((Join-Path $rootPath 'sources\main.cpp'), "SOURCE-CANARY`n", [Text.Encoding]::ASCII)
        [IO.File]::WriteAllText((Join-Path $rootPath 'secrets\token.txt'), "SECRET-CANARY`n", [Text.Encoding]::ASCII)
        $verify = @{ Root = $rootPath; ExpectedBackend = $Mechanism.ToLowerInvariant(); ExpectedDesktop = 'DR_ZIS_POC' }
        if ($Mechanism -eq 'Classic') {
            $grant = & (Join-Path $PSScriptRoot 'Grant-AppContainerAccess.ps1') -Root $rootPath
            $grant | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $caseDir 'classic-profile.json') -Encoding UTF8
            $verify.ExpectedAppContainerSid = $grant.AppContainerSid
            $verify.RequireLowIntegrity = $true
            $arguments = @('--classic', '--file-test', $rootPath)
        } else {
            $mode = if ($Mechanism -eq 'CPIS') { '--experimental' } else { '--psec' }
            $arguments = @($mode, '--file-test', $rootPath, '--file-test-no-ui')
            if ($case -eq 'allow-secrets') { $arguments += '--file-test-allow-secrets'; $verify.Baseline = $true }
            if ($case -eq 'deny-overrides-grant') { $arguments += '--file-test-deny-overrides-grant' }
            if ($Mechanism -eq 'CPIS') { $verify.RequireLowIntegrity = $true }
        }
        $record.LauncherExit = Invoke-Logged $launcher $arguments (Join-Path $caseDir 'launcher.log')
        # Capture actual canaries even on a failed launch or mismatching probe.
        Copy-Item -LiteralPath (Join-Path $rootPath 'sources\main.cpp') -Destination (Join-Path $caseDir 'source.after.txt')
        Copy-Item -LiteralPath (Join-Path $rootPath 'secrets\token.txt') -Destination (Join-Path $caseDir 'secret.after.txt')
        if ($record.LauncherExit -ne 0) { throw "Launcher/probe failed: exit=$($record.LauncherExit). See launcher.log and raw fixture evidence." }
        if ($Mechanism -eq 'CPIS') {
            $launchLog = Get-Content -Raw -LiteralPath (Join-Path $caseDir 'launcher.log')
            if ($launchLog -notmatch '(?m)^Derived SID: (S-1-15-2-[0-9-]+)\r?$') {
                throw 'CPIS launcher did not record its derived identity SID.'
            }
            $verify.ExpectedAppContainerSid = $Matches[1]
        }
        & (Join-Path $PSScriptRoot 'Verify-Fixture.ps1') @verify
        if ($Mechanism -ne 'Classic') {
            $before = [IO.File]::ReadAllText((Join-Path $rootPath 'security-before.txt'))
            $bound = [IO.File]::ReadAllText((Join-Path $rootPath 'security-bound.txt'))
            $after = [IO.File]::ReadAllText((Join-Path $rootPath 'security-after.txt'))
            if ($before -cne $bound -or $before -cne $after) {
                throw 'Host DACL/MIC changed while policy was bound or after execution; independent enforcement is not established.'
            }
        }
        $record.Verified = $true
    } catch {
        $record.Error = $_.Exception.Message
        Write-Warning $record.Error
    }
    $results += [pscustomobject]$record
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'results.json') -Encoding UTF8
}
$allVerified = @($results | Where-Object { -not $_.Verified }).Count -eq 0
$contextsEquivalent = $null
if ($Mechanism -ne 'Classic' -and $allVerified) {
    $contexts = @($cases | ForEach-Object {
        $text = Get-Content -Raw -LiteralPath (Join-Path (Join-Path (Join-Path $evidence $_) 'fixture') 'sources\file-probe-context.txt')
        # PSEC may allocate a distinct package SID for each environment. Compare
        # token kind, IL and Desktop; retain each full SID in the raw context.
        (($text -split '\r?\n' | Where-Object { $_ -match '^(Desktop|Integrity|TokenIsAppContainer)=' }) `
            -replace '(TokenIsAppContainer=[01]); SID=.*', '$1') -join "`n"
    })
    $contextsEquivalent = @($contexts | Select-Object -Unique).Count -eq 1
}
[pscustomobject]@{ Mechanism = $Mechanism; OperationsAndControlsVerified = $allVerified
    ComparableContextsVerified = $contextsEquivalent
    IndependentEnforcementVerified = ($Mechanism -ne 'Classic' -and $allVerified -and $contextsEquivalent)
    VmVerified = $null } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $evidence 'experiment.json') -Encoding UTF8
$results | Format-Table Mechanism, Case, LowBaselineExit, LauncherExit, Verified, Error -AutoSize
if (-not $allVerified -or $contextsEquivalent -eq $false) {
    throw 'Experiment incomplete/failed or control contexts differ; raw errors and evidence have been retained.'
}
Write-Host 'All requested operations, controls, canaries and context checks passed. Record VM identity separately.'
