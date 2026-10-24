param(
    [Parameter(Mandatory = $true)][string]$ProbeExe,
    [string]$Root = (Join-Path $env:LOCALAPPDATA ('ZIS-FsTest-' + [guid]::NewGuid().ToString('N'))),
    [ValidateSet('Classic', 'CPIS', 'PSEC')][string]$Mechanism = 'Classic'
)
$ErrorActionPreference = 'Stop'
$probe = (Resolve-Path -LiteralPath $ProbeExe).ProviderPath
if ([IO.Path]::GetFileName($probe) -ine 'zis_probe.exe') { throw 'ProbeExe must be the existing zis_probe.exe.' }
$rootPath = [IO.Path]::GetFullPath($Root)
if (Test-Path -LiteralPath $rootPath) {
    throw 'Root already exists. Use a fresh fixture for every run; nothing was overwritten.'
}
$userSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
New-Item -ItemType Directory -Path $rootPath | Out-Null
# Own-user and SYSTEM access only; no inherited AppContainer grants.
& icacls.exe $rootPath /inheritance:r | Out-Host
if ($LASTEXITCODE -ne 0) { throw "icacls /inheritance:r failed: exit=$LASTEXITCODE" }
& icacls.exe $rootPath /grant:r "*${userSid}:(OI)(CI)F" '*S-1-5-18:(OI)(CI)F' | Out-Host
if ($LASTEXITCODE -ne 0) { throw "icacls /grant:r failed: exit=$LASTEXITCODE" }
foreach ($name in @('bin', 'sources', 'secrets')) {
    New-Item -ItemType Directory -Path (Join-Path $rootPath $name) | Out-Null
}
Copy-Item -LiteralPath $probe -Destination (Join-Path $rootPath 'bin\zis_probe.exe')
[IO.File]::WriteAllText((Join-Path $rootPath 'sources\main.cpp'), "SOURCE-CANARY`n", [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $rootPath 'secrets\token.txt'), "SECRET-CANARY`n", [Text.Encoding]::ASCII)
# Low-label the entire disposable fixture. A Low-IL ordinary control should
# still read AND update secrets: denials must not merely come from MIC.
& icacls.exe $rootPath /setintegritylevel '(OI)(CI)L' /T | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Failed to set mandatory labels.' }
$cpis = [ordered]@{
    version = '0.1.0'; app_container = $true
    fs_read_only = @((Join-Path $rootPath 'bin'))
    fs_read_write = @((Join-Path $rootPath 'sources'))
    fs_deny = @((Join-Path $rootPath 'secrets'))
}
$psec = [ordered]@{
    version = [ordered]@{ major = 1; minor = 1 }
    fs_read_only = @((Join-Path $rootPath 'bin'))
    fs_read_write = @((Join-Path $rootPath 'sources'))
    fs_deny = @((Join-Path $rootPath 'secrets'))
}
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $rootPath 'cpis-files.json'), ($cpis | ConvertTo-Json -Depth 5), $utf8)
[IO.File]::WriteAllText((Join-Path $rootPath 'psec-files.json'), ($psec | ConvertTo-Json -Depth 5), $utf8)
$identity = if ($Mechanism -eq 'Classic') { 'IgorKorkin.ZIS.PoC' }
            elseif ($Mechanism -eq 'CPIS') { 'IgorKorkin.ZIS.CPIS.FileTest' } else { $null }
$fixture = [pscustomobject]@{ Root = $rootPath; UserSid = $userSid; Mechanism = $Mechanism; LauncherIdentity = $identity }
[IO.File]::WriteAllText((Join-Path $rootPath 'fixture.json'), ($fixture | ConvertTo-Json), $utf8)
Write-Host 'Fresh fixture prepared. JSON is descriptive; launcher builds and saves the actual binary WinAPI specification.'
Write-Host 'No AppContainer filesystem grants were added. Use Grant-AppContainerAccess.ps1 only for Classic.'
$fixture
