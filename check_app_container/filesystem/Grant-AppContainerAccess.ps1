param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$AppContainerSid
)
$ErrorActionPreference = 'Stop'
$profileName = 'IgorKorkin.ZIS.PoC' # Must match the existing Classic launcher.
if (-not ('ZisFileTest.ProfileSid' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace ZisFileTest {
    public static class ProfileSid {
        [DllImport("userenv.dll", CharSet = CharSet.Unicode)]
        public static extern int DeriveAppContainerSidFromAppContainerName(string name, out IntPtr sid);
        [DllImport("advapi32.dll")]
        public static extern IntPtr FreeSid(IntPtr sid);
    }
}
'@
}
$nativeSid = [IntPtr]::Zero
$hr = [ZisFileTest.ProfileSid]::DeriveAppContainerSidFromAppContainerName($profileName, [ref]$nativeSid)
if ($hr -lt 0) { throw ('DeriveAppContainerSid failed: HRESULT=0x{0:X8}' -f ($hr -band 0xffffffffL)) }
try { $expectedSid = [Security.Principal.SecurityIdentifier]::new($nativeSid).Value }
finally { [void][ZisFileTest.ProfileSid]::FreeSid($nativeSid) }
if ($AppContainerSid -and $AppContainerSid -cne $expectedSid) {
    throw "SID does not match launcher profile ${profileName}: expected $expectedSid"
}
$AppContainerSid = $expectedSid
$rootPath = (Resolve-Path -LiteralPath $Root).ProviderPath
$manifest = Join-Path $rootPath 'fixture.json'
if (Test-Path -LiteralPath $manifest) {
    if ((Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json).Mechanism -ne 'Classic') {
        throw 'Classic DACL grants are forbidden on a CPIS/PSEC fixture.'
    }
}
foreach ($relative in @('sources\main.cpp', 'secrets\token.txt', 'bin\zis_probe.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $rootPath $relative))) { throw 'Incomplete fixture.' }
}
# /grant changes only the named DACL ACEs; it does not rewrite SACL or MIC.
foreach ($item in @(@($rootPath, 'RX'), @((Join-Path $rootPath 'bin'), '(OI)(CI)RX'),
                    @((Join-Path $rootPath 'sources'), '(OI)(CI)M'))) {
    & icacls.exe $item[0] /grant ("*${AppContainerSid}:" + $item[1]) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "icacls /grant failed for $($item[0]): exit=$LASTEXITCODE" }
}
Write-Host "Verified launcher profile: $profileName; SID: $AppContainerSid"
Write-Host 'Classic grants added: bin=RX, sources=Modify, secrets=no grant.'
Write-Host 'Do not reuse this fixture/profile for CPIS or PSEC.'
[pscustomobject]@{ Root = $rootPath; ProfileName = $profileName; AppContainerSid = $AppContainerSid }
