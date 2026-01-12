# Visualizing Your Bot - Render Mode Guide

This guide explains how to watch your bot play while it trains using GigaLearn's render mode.

## Overview

GigaLearn includes a RenderSender utility to visualize your bot in real-time. The `--render` flag allows you to run a separate instance that renders gameplay without interfering with training.

## Quick Start

### Method 1: Manual Rendering (Simple)

1. **Start training** (runs at maximum speed):
   ```bash
   ./build/GigaLearnBot.exe
   ```

2. **Open RocketSimVis** (or another RocketSim visualizer)

3. **Start render instance** in a new terminal:
   ```bash
   ./build/GigaLearnBot.exe --render
   ```

4. Watch your bot play in the visualizer!

### Method 2: Auto-Relaunch (Recommended)

The auto-relaunch script automatically restarts the render instance when new checkpoints are saved, so you always see your bot's latest progress.

**Using PowerShell:**
```powershell
.\auto_render.ps1
```

**Using Batch File:**
```batch
auto_render.bat
```

**Custom Configuration:**
```powershell
.\auto_render.ps1 -ExePath ".\build\GigaLearnBot.exe" -CheckpointFolder ".\checkpoints" -CheckIntervalSeconds 10
```

## What `--render` Does

When you run with the `--render` flag, the following changes are applied:

| Setting | Training Mode | Render Mode |
|---------|--------------|-------------|
| `renderMode` | `false` | `true` |
| `sendMetrics` | `true` | `false` |
| `deterministic` | `false` | `true` |
| Speed | Maximum | Watchable (3x real-time) |

**Key Points:**
- **Deterministic mode**: The bot uses its best learned actions (no exploration/randomness)
- **No metrics**: Render instance doesn't log to WandB or other metric systems
- **Separate process**: Training continues at full speed in the main instance

## Workflow

### Typical Usage Pattern

```
Terminal 1: Training Instance
├── Runs at maximum speed (~8B steps/day)
├── Saves checkpoints every 100M steps
├── Logs metrics to WandB
└── Never stops

Terminal 2: Render Instance (Auto-Relaunch)
├── Loads latest checkpoint on start
├── Runs at watchable speed (3x real-time)
├── Auto-restarts when new checkpoint detected
└── Shows bot's current best performance

Visualizer: RocketSimVis
└── Connects to render instance
```

### Step-by-Step Workflow

1. **Build your bot:**
   ```bash
   cd build
   cmake --build . --config Release
   ```

2. **Start training in one terminal:**
   ```bash
   ./build/GigaLearnBot.exe
   ```

3. **Wait for first checkpoint** (about 100M steps, ~45 minutes)

4. **Launch RocketSimVis** (download from RocketSim repo)

5. **Start auto-render script in another terminal:**
   ```bash
   ./auto_render.bat
   ```

6. **Watch your bot improve** over time as new checkpoints are loaded!

## Auto-Relaunch Script Features

### What It Does

- ✅ Monitors checkpoint folder for new `.binn` files
- ✅ Automatically stops old render instance
- ✅ Starts new render instance with latest checkpoint
- ✅ Handles crashes and restarts
- ✅ Color-coded console output
- ✅ Graceful shutdown on Ctrl+C

### Configuration

Edit `auto_render.ps1` parameters:

```powershell
param(
    [string]$ExePath = ".\build\GigaLearnBot.exe",        # Path to executable
    [string]$CheckpointFolder = ".\checkpoints",           # Checkpoint directory
    [int]$CheckIntervalSeconds = 10                        # Check frequency
)
```

### Troubleshooting

**Script won't run:**
```powershell
# Enable PowerShell script execution
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

**Executable not found:**
```powershell
# Specify custom path
.\auto_render.ps1 -ExePath "C:\path\to\GigaLearnBot.exe"
```

**Wrong checkpoint folder:**
```powershell
# Specify custom checkpoint folder
.\auto_render.ps1 -CheckpointFolder "C:\path\to\checkpoints"
```

**Process crashes repeatedly:**
- Check if RocketSimVis is running
- Verify checkpoint files aren't corrupted
- Check console output for error messages

## Visualizer Setup

### RocketSimVis (Recommended)

1. Clone the RocketSim repository
2. Build RocketSimVis (instructions in RocketSim docs)
3. Run RocketSimVis before starting render instance
4. It will automatically connect when bot starts

### Other Visualizers

Any RocketSim-compatible visualizer should work. Make sure it's running before starting the render instance.

## Performance Notes

### Training Instance Performance

- Runs at **maximum simulation speed**
- Typically achieves **8-12 billion steps per day** on modern GPUs
- Uses all available GPU resources
- No visualization overhead

### Render Instance Performance

- Runs at **3x real-time speed** (configurable via `renderTimeScale`)
- Minimal GPU usage (just for inference)
- Does not interfere with training instance
- Can be stopped/started without affecting training

## Advanced Usage

### Viewing Specific Checkpoints

To view a specific checkpoint instead of the latest:

1. Stop the auto-render script
2. Manually copy desired checkpoint to be the newest (change timestamp)
3. Restart render instance

### Multiple Render Instances

You can run multiple render instances to compare different checkpoints:

```bash
# Terminal 1: Latest checkpoint
./build/GigaLearnBot.exe --render

# Terminal 2: Specific older checkpoint (after swapping files)
./build/GigaLearnBot.exe --render
```

Note: You'll need separate RocketSimVis instances or different ports.

### Deterministic vs. Stochastic Rendering

By default, render mode uses **deterministic** policy (best actions). To see how the bot plays during training (with exploration):

Edit `ExampleMain.cpp` line 1011:
```cpp
cfg.ppo.deterministic = false;  // Show training behavior
```

Then rebuild and run with `--render`.

## Implementation Details

### Code Location

The `--render` flag is parsed in `ExampleMain.cpp` at **line 924-932**:

```cpp
// Parse command-line arguments for render mode
bool renderMode = false;
for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--render") {
        renderMode = true;
        std::cout << "[CONFIG] Render mode enabled\n";
        break;
    }
}
```

Configuration is applied at **line 1007-1016**:

```cpp
// Configure render mode settings
if (renderMode) {
    cfg.sendMetrics = false;           // No need to log metrics from render instance
    cfg.renderMode = true;             // Enable visualization
    cfg.ppo.deterministic = true;      // Use best actions (no exploration)
    std::cout << "[CONFIG] Rendering with deterministic policy\n";
} else {
    cfg.sendMetrics = true;
    cfg.renderMode = false;
}
```

### How It Works

1. **Checkpoint Loading**: Render instance loads the most recent checkpoint file
2. **Inference Only**: No training happens, only policy inference
3. **Render Sender**: GigaLearn's RenderSender sends game state to visualizer
4. **Time Scaling**: Simulation slowed to `renderTimeScale` (default 3x real-time)

## FAQ

**Q: Can I train and render simultaneously?**
A: Yes! That's the whole point. Training runs at full speed, render runs separately.

**Q: Will rendering slow down training?**
A: No. They're separate processes with separate GPU contexts.

**Q: How often should I update the render instance?**
A: The auto-script checks every 10 seconds by default. Adjust as needed.

**Q: Can I render without training?**
A: Yes! Just run the render instance alone. It will load the latest checkpoint.

**Q: Does render mode work with custom rewards?**
A: Yes. Render mode just visualizes gameplay, regardless of reward configuration.

**Q: Can I record gameplay?**
A: Use your visualizer's recording features (RocketSimVis may support this).

## Checkpoint Schedule

Based on default settings (`tsPerSave = 100'000'000`):

| Time (8B steps/day) | Steps | Checkpoint |
|---------------------|-------|------------|
| ~45 min | 100M | ✅ First checkpoint |
| ~1.5 hrs | 200M | ✅ Second checkpoint |
| ~3 hrs | 400M | ✅ Visible progress |
| ~6 hrs | 800M | ✅ Basic mechanics |
| ~12 hrs | 1.6B | ✅ Silver level |
| ~24 hrs | 3.2B | ✅ Gold level |

Adjust `cfg.tsPerSave` in ExampleMain.cpp to save more/less frequently.

## Support

If you encounter issues:

1. Check that paths in the script match your setup
2. Verify RocketSim is initialized correctly
3. Ensure checkpoint files aren't corrupted
4. Check GPU memory isn't exhausted
5. Review console output for error messages

Happy visualizing! 🚀
