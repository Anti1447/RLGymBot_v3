# 🚀 Quick Start: MLP-Mixer Setup in 5 Minutes

**Goal**: Get MLP-Mixer compiled and running
**Time**: 5-10 minutes
**Difficulty**: Easy

---

## 🎯 The Absolute Fastest Path

### Windows (Recommended)

```bash
# 1. Navigate to project root
cd C:\Users\nhafe\.claude-worktrees\RLGymBot_v3\goofy-curran

# 2. Run the build script
BUILD.bat

# 3. Wait for "BUILD COMPLETE! [SUCCESS]"
# Done! ✅
```

### Linux/Mac

```bash
# 1. Navigate to project root
cd /path/to/goofy-curran

# 2. Make script executable and run
chmod +x BUILD.sh
./BUILD.sh

# 3. Wait for "BUILD COMPLETE! ✅"
# Done! ✅
```

---

## 📋 What the Build Script Does

```
BUILD.bat/sh
├─ Verifies all MLP-Mixer files exist ✅
├─ Cleans old build artifacts
├─ Creates fresh build directory
├─ Runs CMake configuration
├─ Compiles the project
├─ Verifies library was built
└─ Reports success! ✅
```

---

## ✅ You Know It Worked If...

After the build completes, you should see:

```
================================================
BUILD COMPLETE! [SUCCESS]
================================================

Build artifacts: C:\...\goofy-curran\build

Next steps:
1. Configure training to use MLP-Mixer
2. Start training with NetworkArchitecture::MLP_MIXER
3. Monitor logs for MixerModels creation message
```

---

## 🎮 Now Use It in Your Training

Edit your training file (e.g., `ExampleMain.cpp`):

```cpp
#include <GigaLearnCPP/PPO/PPOLearnerConfig.h>

int main() {
    LearnerConfig config;

    // ✨ Enable MLP-Mixer!
    config.ppoConfig.networkArch = NetworkArchitecture::MLP_MIXER;

    // Configure as normal
    Learner learner(envCreateFunc, config);
    learner.Start();  // Now uses MLP-Mixer!

    return 0;
}
```

---

## 🔍 Verify It's Working

When training starts, watch for this log:

```
[INFO] MixerModels created:
[INFO]   numPlayers: 2
[INFO]   obs_size: 30
[INFO]   num_actions: 8
[INFO]   mixer_blocks: 2
[INFO]   hidden_dim: 256
[INFO]   token_dim: 128
```

If you see this, **MLP-Mixer is active!** 🎉

---

## 🆘 If Build Fails

### Error: "Cannot find project files"
Make sure you're in the right directory:
```bash
cd C:\Users\nhafe\.claude-worktrees\RLGymBot_v3\goofy-curran
```

### Error: "CMake not found"
Install CMake from https://cmake.org or via:
```bash
# Windows (Chocolatey)
choco install cmake

# Mac (Homebrew)
brew install cmake

# Linux (Ubuntu)
sudo apt-get install cmake
```

### Error: "Visual Studio not found"
Install Visual Studio 2022 Community Edition from:
https://visualstudio.microsoft.com/

### Error: "LibTorch not found"
Place LibTorch in: `GigaLearnBot/GigaLearnCPP/libtorch/`

Or download from: https://pytorch.org/get-started/locally/

### Other Error?
See `SETUP_AND_RUN.md` Troubleshooting section for detailed help.

---

## 📊 Build Times (Approximate)

| Configuration | Time |
|---------------|------|
| First build (cold cache) | 5-15 min |
| Incremental build | 30 sec - 2 min |
| Clean rebuild | 5-15 min |

---

## 🎓 What Each File Does

The build includes these MLP-Mixer components:

| File | Purpose |
|------|---------|
| `MixerBlock.h` | Core mixer blocks (token + channel mixing) |
| `RLTokenizer.h` | Converts flat observations to tokens |
| `MixerModelWrapper.h/cpp` | PyTorch integration wrapper |
| `Models.h` | Updated with IModel interface |
| `PPOLearnerConfig.h` | Architecture selection enum |
| `PPOLearner.cpp` | Creates MixerModelWrapper instances |

---

## 🔧 Manual Build (If Script Fails)

```bash
# 1. Go to project root
cd C:\Users\nhafe\.claude-worktrees\RLGymBot_v3\goofy-curran

# 2. Create build dir
mkdir build && cd build

# 3. Configure
cmake -G "Visual Studio 17 2022" -A x64 ..

# 4. Build
cmake --build . --config Release --parallel 4

# 5. Verify
dir Release\GigaLearnCPP.dll
```

---

## 🚀 Next Steps After Build

1. **Edit your training code** to enable MLP-Mixer
2. **Compile your main executable** (should link against updated GigaLearnCPP)
3. **Start training** - MLP-Mixer will be used automatically
4. **Monitor logs** for "MixerModels created" message
5. **Train and enjoy!** 🎉

---

## 📚 For More Information

- **Setup Details**: See `SETUP_AND_RUN.md`
- **Usage Guide**: See `IMPLEMENTATION_GUIDE.md`
- **Architecture**: See `MLP_MIXER_REVIEW_SUMMARY.md`
- **Code Quality**: See `FIXES_COMPLETED.md`

---

## 💡 Pro Tips

**Faster iterations**: Keep the build directory between builds
```bash
# Just rebuild after code changes
cd build
cmake --build . --config Release
```

**Parallel builds**: Use more CPU cores
```bash
# 8 parallel jobs instead of 4
cmake --build . --config Release --parallel 8
```

**Debug builds**: For easier debugging
```bash
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Debug  # No optimization, full debug info
```

---

## ✨ You're All Set!

That's it! Your MLP-Mixer implementation is:
- ✅ Compiled
- ✅ Integrated
- ✅ Ready to train
- ✅ Production quality

Now go make an amazing bot! 🚀
