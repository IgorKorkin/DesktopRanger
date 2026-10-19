[CmdletBinding()]
param(
    [string]$Repository = (Split-Path -Parent $PSScriptRoot),
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$TestFilter = '*',
    [switch]$TestsOnly,
    [string]$LogDirectory
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$result = 1
$locationPushed = $false
try {
    $Repository = (Resolve-Path -LiteralPath $Repository).Path
    $solution = Join-Path $Repository 'DesktopRanger.sln'
    if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) { throw "Solution not found: $solution" }
    $buildTarget = $solution
    if ($TestsOnly) { $buildTarget = Join-Path $Repository 'desktop.ranger.tests\desktop.ranger.tests.vcxproj' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere not found: $vswhere" }
    $msbuild = @(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
    if ($LASTEXITCODE -ne 0 -or $msbuild.Count -eq 0) { throw 'MSBuild was not found.' }
    $msbuild = $msbuild[0]
    if (-not $LogDirectory) {
        $LogDirectory = Join-Path $Repository ('.output\' + $Configuration + '\logs\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    }
    $null = New-Item -ItemType Directory -Force -Path $LogDirectory
    $LogDirectory = (Resolve-Path -LiteralPath $LogDirectory).Path
    $buildLog = Join-Path $LogDirectory 'build.log'
    $testLog = Join-Path $LogDirectory 'tests.log'
    $testXml = Join-Path $LogDirectory 'test-results.xml'
    if (Test-Path -LiteralPath $testXml) { Remove-Item -LiteralPath $testXml }
    Push-Location -LiteralPath $Repository
    $locationPushed = $true
    Write-Host "MSBuild: $msbuild"
    & $msbuild $buildTarget /m:1 /nr:false /nologo "/p:SolutionDir=$Repository\" "/p:Configuration=$Configuration" /p:Platform=x64 /p:PlatformToolset=v145 /p:PreferredToolArchitecture=x64 /verbosity:minimal /fl "/flp:logfile=$buildLog;verbosity=diagnostic"
    if ($LASTEXITCODE -ne 0) {
        $result = $LASTEXITCODE
        throw "Build failed ($result). Log: $buildLog"
    }
    $testExecutable = Join-Path $Repository ".output\$Configuration\Desktop.Ranger.Tests.exe"
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) { throw "Test executable not found: $testExecutable" }
    # Windows PowerShell 5.1 turns redirected native stderr into ErrorRecords.
    # Keep collecting output and judge the native process by its exit code.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $testExecutable "--gtest_filter=$TestFilter" "--gtest_output=xml:$testXml" 2>&1 | Tee-Object -FilePath $testLog -ErrorAction Stop
        $testExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousErrorActionPreference }
    if ($testExit -ne 0) {
        $result = $testExit
        throw "GoogleTest failed ($result). Log: $testLog"
    }
    if (-not (Test-Path -LiteralPath $testXml -PathType Leaf)) { throw "GoogleTest did not create XML: $testXml" }
    [xml]$report = Get-Content -LiteralPath $testXml -Raw
    $total = [int]$report.testsuites.tests
    $failed = [int]$report.testsuites.failures + [int]$report.testsuites.errors
    $skipped = @($report.SelectNodes('//testcase[@status="notrun" or @result="skipped" or skipped]')).Count
    if ($total -eq 0 -or $failed -ne 0) { throw 'XML reports zero tests or test failures.' }
    Write-Host "Tests: $total; passed: $($total - $failed - $skipped); failed: $failed; skipped: $skipped"
    Write-Host "Logs and XML: $LogDirectory"
    $result = 0
}
catch { Write-Error -Message $_.Exception.Message -ErrorAction Continue }
finally { if ($locationPushed) { Pop-Location } }
exit $result

