# ============================================================================
# Auto-Relaunch Render Script for GigaLearnBot
# ============================================================================
# This script monitors the checkpoint folder and automatically relaunches
# the render instance when a new checkpoint is detected.
#
# Usage:
#   .\auto_render.ps1
#
# Requirements:
#   - GigaLearnBot.exe must be built and accessible
#   - RocketSimVis or another visualizer should be running to connect to the bot
# ============================================================================

param(
    [string]$ExePath = ".\build\bin\GigaLearnBot.exe",
    [string]$CheckpointFolder = "C:\Projects\RLGymBot_v3\GigaLearnBot\build\checkpoints",
    [int]$CheckIntervalSeconds = 15
)

# Color output functions
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

# Validate executable exists
if (-not (Test-Path $ExePath)) {
    Write-Error "GigaLearnBot executable not found at: $ExePath"
    Write-Info "Please build the project first or specify the correct path with -ExePath"
    exit 1
}

# Validate checkpoint folder exists
if (-not (Test-Path $CheckpointFolder)) {
    Write-Error "Checkpoint folder not found at: $CheckpointFolder"
    Write-Info "Creating checkpoint folder..."
    New-Item -ItemType Directory -Path $CheckpointFolder -Force | Out-Null
}

Write-Info "==================================================================="
Write-Info "GigaLearnBot Auto-Render Script"
Write-Info "==================================================================="
Write-Info "Executable: $ExePath"
Write-Info "Checkpoint Folder: $CheckpointFolder"
Write-Info "Check Interval: $CheckIntervalSeconds seconds"
Write-Info "==================================================================="
Write-Info ""
Write-Warning "Make sure RocketSimVis is running before the bot starts!"
Write-Info ""

# Function to validate if a folder is a valid checkpoint
function Test-ValidCheckpoint {
    param([string]$FolderPath)

    $requiredFiles = @(
        "CRITIC.lt",
        "CRITIC_OPTIM.lt",
        "POLICY.lt",
        "POLICY_OPTIM.lt",
        "PPO_POLICY_SD.pt",
        "PPO_POLICY_SD_META.json",
        "RUNNING_STATS.json",
        "SHARED_HEAD.lt",
        "SHARED_HEAD_OPTIM.lt"
    )

    foreach ($file in $requiredFiles) {
        $filePath = Join-Path $FolderPath $file
        if (-not (Test-Path $filePath)) {
            return $false
        }
    }

    return $true
}

# Function to get the latest checkpoint folder
function Get-LatestCheckpoint {
    $checkpointDirs = Get-ChildItem -Path $CheckpointFolder -Directory -ErrorAction SilentlyContinue

    if ($checkpointDirs.Count -eq 0) {
        return $null
    }

    # Filter for valid checkpoints only
    $validCheckpoints = $checkpointDirs | Where-Object { Test-ValidCheckpoint $_.FullName }

    if ($validCheckpoints.Count -eq 0) {
        return $null
    }

    # Return the most recently modified valid checkpoint
    return ($validCheckpoints | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
}

# Function to start the render process with specific checkpoint
function Start-RenderProcess {
    param([string]$CheckpointPath)

    # GigaLearn loads the latest checkpoint from the checkpoint folder automatically
    # The --render flag enables:
    #   - Deterministic policy (best actions, no exploration)
    #   - 3 parallel games with fixed modes: 1v1, 2v2, 3v3
    #   - Connection to RocketSimVis for visualization
    # Run from the build directory (not bin) to ensure correct relative path resolution
    $buildDir = Split-Path -Parent (Resolve-Path $ExePath)
    $workingDir = Split-Path -Parent $buildDir
    $process = Start-Process -FilePath $ExePath -ArgumentList "--render" -WorkingDirectory $workingDir -PassThru -NoNewWindow
    Write-Success "Render instance started (PID: $($process.Id))"
    Write-Info "Working directory: $workingDir"
    Write-Info "Loading checkpoint: $CheckpointPath"
    Write-Info "Game modes: 1v1 (env 0), 2v2 (env 1), 3v3 (env 2) - deterministic"
    return $process
}

# Function to stop the render process
function Stop-RenderProcess {
    param([System.Diagnostics.Process]$Process)

    if ($null -ne $Process -and -not $Process.HasExited) {
        Write-Info "Stopping render instance (PID: $($Process.Id))..."
        $Process.Kill()
        $Process.WaitForExit(5000)
        Write-Success "Render instance stopped"
    }
}

# Main loop
$currentCheckpoint = Get-LatestCheckpoint
$renderProcess = $null

if ($null -eq $currentCheckpoint) {
    Write-Warning "No valid checkpoint folders found yet. Waiting for first checkpoint..."
    Write-Info "Checkpoint folder must contain:"
    Write-Info "  - CRITIC.lt, CRITIC_OPTIM.lt"
    Write-Info "  - POLICY.lt, POLICY_OPTIM.lt"
    Write-Info "  - PPO_POLICY_SD.pt, PPO_POLICY_SD_META.json"
    Write-Info "  - RUNNING_STATS.json"
    Write-Info "  - SHARED_HEAD.lt, SHARED_HEAD_OPTIM.lt"
} else {
    Write-Info "Latest checkpoint folder: $($currentCheckpoint.Name) (Modified: $($currentCheckpoint.LastWriteTime))"
    Write-Info "Starting render instance..."
    $renderProcess = Start-RenderProcess -CheckpointPath $currentCheckpoint.FullName
}

Write-Info "Monitoring for new checkpoint folders... (Press Ctrl+C to stop)"
Write-Info ""

try {
    while ($true) {
        Start-Sleep -Seconds $CheckIntervalSeconds

        $latestCheckpoint = Get-LatestCheckpoint

        if ($null -eq $latestCheckpoint) {
            if ($null -ne $renderProcess) {
                Write-Warning "No valid checkpoint folders found (deleted?). Stopping render instance."
                Stop-RenderProcess -Process $renderProcess
                $renderProcess = $null
                $currentCheckpoint = $null
            }
            continue
        }

        # Check if this is a new checkpoint folder or updated checkpoint
        $isNewCheckpoint = $false
        if ($null -eq $currentCheckpoint) {
            $isNewCheckpoint = $true
        } elseif ($latestCheckpoint.Name -ne $currentCheckpoint.Name) {
            # Different folder name = new checkpoint
            $isNewCheckpoint = $true
        } elseif ($latestCheckpoint.LastWriteTime -gt $currentCheckpoint.LastWriteTime) {
            # Same folder but newer timestamp = updated checkpoint
            $isNewCheckpoint = $true
        }

        if ($isNewCheckpoint) {
            Write-Info "-------------------------------------------------------------------"
            Write-Success "New/updated checkpoint detected: $($latestCheckpoint.Name)"
            Write-Info "Modified: $($latestCheckpoint.LastWriteTime)"

            # Calculate total size of checkpoint folder
            $checkpointSize = (Get-ChildItem -Path $latestCheckpoint.FullName -Recurse -File |
                              Measure-Object -Property Length -Sum).Sum
            Write-Info "Size: $([math]::Round($checkpointSize / 1MB, 2)) MB"

            # List files in checkpoint
            Write-Info "Files found:"
            Get-ChildItem -Path $latestCheckpoint.FullName -File | ForEach-Object {
                Write-Info "  - $($_.Name)"
            }

            # Stop old render instance
            if ($null -ne $renderProcess) {
                Stop-RenderProcess -Process $renderProcess
            }

            # Wait a moment for files to finish writing
            Start-Sleep -Seconds 2

            # Start new render instance
            $renderProcess = Start-RenderProcess -CheckpointPath $latestCheckpoint.FullName
            $currentCheckpoint = $latestCheckpoint
            Write-Info "-------------------------------------------------------------------"
            Write-Info ""
        }

        # Check if render process crashed
        if ($null -ne $renderProcess -and $renderProcess.HasExited) {
            Write-Warning "Render process exited unexpectedly (Exit code: $($renderProcess.ExitCode))"
            Write-Info "Restarting render instance..."
            if ($null -ne $currentCheckpoint) {
                $renderProcess = Start-RenderProcess -CheckpointPath $currentCheckpoint.FullName
            } else {
                $renderProcess = Start-RenderProcess -CheckpointPath ""
            }
            Write-Info ""
        }
    }
}
finally {
    # Cleanup on exit
    Write-Info ""
    Write-Info "Shutting down..."
    Stop-RenderProcess -Process $renderProcess
    Write-Success "Auto-render script stopped"
}
