#Requires -Version 5.1
# Build wrapper with progress bar. Run from GigaLearnBot/.
# Usage: .\build_progress.ps1 [-Config Release|Debug] [-Jobs 4]

param(
    [string]$Config = "Release",
    [int]$Jobs = 4
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir  = Join-Path $scriptDir "build"

if (-not (Test-Path $buildDir)) {
    Write-Host "Build directory not found: $buildDir" -ForegroundColor Red
    Write-Host "Run cmake configure first." -ForegroundColor Yellow
    exit 1
}

# Pre-count .cpp source files (skip build output and libtorch vendor)
$totalFiles = (Get-ChildItem -Path $scriptDir -Recurse -Filter "*.cpp" |
    Where-Object { $_.FullName -notmatch '\\build\\' -and $_.FullName -notmatch '\\libtorch\\' } |
    Measure-Object).Count

Write-Host ""
Write-Host "  Building $Config  |  $Jobs parallel jobs  |  ~$totalFiles source files" -ForegroundColor Cyan
Write-Host ""

$startTime  = Get-Date
$compiled   = 0
$lastStatus = ""

# Stream cmake output line-by-line; update progress bar as .cpp filenames appear
& cmake --build "$buildDir" --config $Config --parallel $Jobs | ForEach-Object {
    $line = [string]$_
    # MSBuild prints the bare filename (e.g. "  RewardConfig.cpp") while compiling
    if ($line -match '^\s{0,4}(\w[\w. ]+\.cpp)\s*$') {
        $compiled++
        $pct        = [Math]::Min(100, [int]($compiled * 100 / [Math]::Max($totalFiles, 1)))
        $lastStatus = "$compiled / $totalFiles  ($($Matches[1].Trim()))"
        Write-Progress -Activity "Building GigaLearnBot ($Config)" `
                       -Status $lastStatus `
                       -PercentComplete $pct
    }
}

$exitCode = $LASTEXITCODE
Write-Progress -Activity "Building GigaLearnBot ($Config)" -Completed

$elapsed    = (Get-Date) - $startTime
$elapsedStr = "{0:mm\:ss}" -f $elapsed

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "  Build succeeded in $elapsedStr" -ForegroundColor Green
} else {
    Write-Host "  Build FAILED (exit $exitCode) after $elapsedStr" -ForegroundColor Red
}
Write-Host ""

exit $exitCode
