[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$Repository = (Split-Path -Parent $PSScriptRoot),
    [string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
try {
    $Repository = (Resolve-Path -LiteralPath $Repository).Path
    $runner = Join-Path $Repository ".output\$Configuration\functional_test_runner.exe"
    $probe = Join-Path $Repository ".output\$Configuration\test_probe.exe"
    if (-not (Test-Path -LiteralPath $runner) -or -not (Test-Path -LiteralPath $probe)) {
        throw 'Build the solution first: runner or precompiled probe is missing.'
    }
    if (-not $ReportDirectory) {
        $ReportDirectory = Join-Path $Repository ".output\$Configuration\functional"
    }
    $null = New-Item -ItemType Directory -Force -Path $ReportDirectory
    $ReportDirectory = (Resolve-Path -LiteralPath $ReportDirectory).Path
    $report = Join-Path $ReportDirectory 'functional-results.txt'
    if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report }
    $process = Start-Process -FilePath $runner -WindowStyle Hidden -PassThru `
        -ArgumentList @('--report', ('"' + $report + '"')) `
        -RedirectStandardOutput (Join-Path $ReportDirectory 'runner.log') `
        -RedirectStandardError (Join-Path $ReportDirectory 'runner-errors.log')
    try {
        if (-not $process.WaitForExit(60000)) {
            $process.Kill()
            $null = $process.WaitForExit(1000)
            throw 'Runner exceeded 60 seconds. Probe self-cleanup is bounded by 15 seconds after its loop starts.'
        }
        $exitCode = $process.ExitCode
    }
    finally { $process.Dispose() }
    if (-not (Test-Path -LiteralPath $report)) { throw 'Runner did not create a report.' }
    $lines = Get-Content -LiteralPath $report
    $lines | Write-Output
    if ($exitCode -ne 0 -or $lines -notcontains 'SUMMARY: passed=3 failed=0') {
        throw "Current-Desktop integration failed ($exitCode). See $ReportDirectory"
    }
    exit 0
}
catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
}
