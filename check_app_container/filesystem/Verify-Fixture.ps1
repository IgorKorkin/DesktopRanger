param(
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$Baseline,
    [string]$ExpectedBackend,
    [string]$ExpectedDesktop,
    [string]$ExpectedAppContainerSid,
    [switch]$RequireLowIntegrity,
    [switch]$RequireOrdinaryToken
)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path $Root 'sources\main.cpp'))
$secret = [IO.File]::ReadAllText((Join-Path $Root 'secrets\token.txt'))
if ($source -cne "XOURCE-CANARY`n") { throw 'Source update did not match the expected one-byte change.' }
$expectedSecret = if ($Baseline) { "XECRET-CANARY`n" } else { "SECRET-CANARY`n" }
if ($secret -cne $expectedSecret) { throw 'Secret contents changed unexpectedly, or baseline update failed.' }
$rows = @(Import-Csv -LiteralPath (Join-Path $Root 'sources\file-probe-results.csv'))
if ($rows.Count -ne 4) { throw 'CSV does not contain exactly four observations.' }
$operations = @('sources.read', 'sources.update', 'secrets.read', 'secrets.update')
for ($i = 0; $i -lt 4; $i++) {
    $expected = if ($i -lt 2 -or $Baseline) { 'ALLOWED' } else { 'DENIED' }
    $code = if ($expected -eq 'ALLOWED') { '0' } else { '5' }
    $row = $rows[$i]
    if ($row.operation -cne $operations[$i] -or $row.expected -cne $expected -or
        $row.actual -cne $expected -or $row.error_domain -cne 'Win32' -or
        $row.error_code -cne $code -or $row.match -cne 'true' -or
        ($ExpectedBackend -and $row.backend -cne $ExpectedBackend)) {
        throw "Unexpected CSV observation at row $($i + 1)."
    }
}
$context = Get-Content -Raw -LiteralPath (Join-Path $Root 'sources\file-probe-context.txt')
if ($ExpectedDesktop -and $context -cnotmatch ('(?m)^Desktop=' + [regex]::Escape($ExpectedDesktop) + '\r?$')) {
    throw 'Actual child Desktop does not match.'
}
if ($RequireLowIntegrity -and $context -cnotmatch '(?m)^Integrity=Low \(RID 4096\)\r?$') {
    throw 'Actual child integrity is not Low.'
}
if ($RequireOrdinaryToken -and $context -cnotmatch '(?m)^TokenIsAppContainer=0\r?$') {
    throw 'The ordinary Low-IL control unexpectedly used an AppContainer token.'
}
if ($ExpectedAppContainerSid -and $context -cnotmatch
    ('(?m)^TokenIsAppContainer=1; SID=' + [regex]::Escape($ExpectedAppContainerSid) + '\r?$')) {
    throw 'Actual child AppContainer SID does not match the launcher profile.'
}
$rows | Format-Table backend,operation,expected,actual,error_domain,error_code,match
Write-Host 'Verified actual file contents and four CSV observations.'
