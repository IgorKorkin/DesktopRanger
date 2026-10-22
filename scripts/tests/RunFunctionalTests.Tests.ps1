[CmdletBinding()]
param(
    [switch]$SimulateRunnerExit,
    [string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($SimulateRunnerExit) {
    function Start-Process {
        param($FilePath, $WindowStyle, [switch]$PassThru, $ArgumentList,
              $RedirectStandardOutput, $RedirectStandardError)
        $process = [pscustomobject]@{ ExitCode = 37 }
        $process | Add-Member ScriptMethod WaitForExit { param($timeout) return $true }
        $process | Add-Member ScriptMethod Dispose { }
        return $process
    }
    & (Join-Path $repository 'scripts/RunFunctionalTests.ps1') `
        -Repository $repository -ReportDirectory $ReportDirectory
    exit $LASTEXITCODE
}
$reportDirectory = Join-Path $repository ('.output/Release/script-tests/' + [guid]::NewGuid())
$shell = (Get-Process -Id $PID).Path
$output = & $shell -NoProfile -File $PSCommandPath -SimulateRunnerExit `
    -ReportDirectory $reportDirectory 2>&1 | Out-String
$exitCode = $LASTEXITCODE
if ($exitCode -ne 1 -or -not $output.Contains('exit code 37') -or
    -not $output.Contains($reportDirectory)) {
    throw "Missing-report diagnostics regression failed: $output"
}
Write-Output 'PASS: missing report preserves runner exit code and log directory.'
exit 0
