# CLAUDE.md — RLGymBot v3

## 1. What is this?

A Rocket League AI bot trained via PPO reinforcement learning. The bot plays 1v1, 2v2, and 3v3.

**Stack:** RLGymCPP (env) → GigaLearnCPP (PPO/LibTorch C++) → RocketSim (physics) → RLBotCPP (deployment)

**Key paths:**
- `GigaLearnBot/src/` — Training entrypoint and config files
- `GigaLearnBot/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/` — All reward classes (~50 files)
- `GigaLearnBot/GigaLearnCPP/src/` — PPO learner, model wrappers, inference
- `GigaLearnBot/RLBotCPP/` — Live game deployment client
- `GigaLearnBot/rlbot/` — RLBot v5 plugin config
- `GigaLearnBot/python_scripts/` — Reward analysis, LLM reward generator, metrics
- `ClaudeFiles/` — Reference docs, code reviews, archived reward files

## 2. How do I run things?

**Build (CMake + MSVC 2022):**
```bash
cd GigaLearnBot
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Release --parallel 4
```
Or use the root `BUILD.bat`. The output is `GigaLearnBot.exe` + `GigaLearnCPP.dll`.

**Run training:**
```bash
cd GigaLearnBot/build
set PYTHONHOME=C:\Users\nhafe\AppData\Local\Programs\Python\Python311
bin\GigaLearnBot.exe
```
Training resumes from the latest checkpoint in `build/checkpoints/`. Render mode uses 3 games (one per mode); training mode uses 310 parallel games.

**Deploy to live Rocket League:** Build the `rlbot` target, then use RLBot v5 + BakkesMod. Config is in `GigaLearnBot/rlbot/`.

## 3. What patterns do I follow?

**Phase-based curriculum.** Everything is gated by `CURRENT_PHASE` in `RewardConfig.h`. Phases go Bronze → SSL (enum `TrainingPhase`). To change phase: edit the constexpr, rebuild.

**Three config files control training:**
- `RewardConfig.h/.cpp` — Reward selection and weights per phase (the big one)
- `ModelConfig.h/.cpp` — LR, entropy, network arch, PPO hyperparams per phase
- `GameModeConfig.h/.cpp` — 1v1/2v2/3v3 sampling distribution

**Reward authoring pattern.** Every reward is a header-only class in `Rewards/` inheriting `Reward`. Override `GetReward(player, state, isFinal)` → return float. Wrap competitive rewards in `ZeroSumReward`. Add to `GetPhasedRewards()` as `WeightedReward(new MyReward(), weight)`.

**Observation:** `AdvancedObsPadded` with max 3 players per team. Padded so the network handles all game modes with one model.

**Action delay:** Training uses 8-tick skip. Live deployment uses 1-tick. This mismatch matters — see `ClaudeFiles/docs/action-delay.md` if it exists, otherwise ask Nathan.

## 4. What's weird here?

- **~50 reward files, 40+ active simultaneously.** Gradient dilution is the #1 problem. Don't add rewards casually — every new reward dilutes the signal from existing ones. Surgical adjustments > dramatic weight changes.
- **Root directory is full of orphaned .md files.** These are session summaries from prior Claude conversations. Ignore them. Real docs live in `ClaudeFiles/`.
- **Three copies of python scripts** exist (`python_scripts/`, `GigaLearnCPP/python_scripts/`, `build/python_scripts/`). The canonical copy is `GigaLearnBot/python_scripts/`.
- **`#pragma once` in .cpp files** — `RewardConfig.cpp` has `#pragma once` at the top. It's nonstandard but works with MSVC. Don't "fix" it.
- **Reward weights span 0.1 to 250.** `GoalReward` is 250, movement rewards are < 1. This is intentional — the scale encodes priority.
- **No unit tests.** Validation is done by compiling, running training, and watching W&B metrics. The `reward_llm_generator.py` has a `--validate-code` mode that does cl.exe syntax checks.
- **`.vs/` and `build/` are gitignored but present.** Don't touch them.

## 5. How do we work?

**Before changing rewards:** Read the relevant reward file first. Understand what phase it's active in. Check `RewardConfig.cpp` for its current weight. Small changes (±20% weight) are safe; large changes risk policy collapse.

**Before adding a new reward:** Ask Nathan which phase it targets. Every reward needs: a header file in `Rewards/`, an include in `RewardConfig.h`, and a `WeightedReward` entry in the correct phase in `RewardConfig.cpp`.

**When modifying training config:** Changes to `ModelConfig.cpp` affect all future training from checkpoint. LR changes, entropy changes, and gamma changes should be gradual (scale over 2-4B steps, not instant).

**Build after every change.** This is C++ — if it doesn't compile, it doesn't exist. Use the CMake build above.

**Don't generate mountains of markdown.** Keep docs in `ClaudeFiles/docs/` if needed. Don't create summary files in the project root.

**Key references:**
- Reward class API: see `Rewards/Reward.h` and `Rewards/CommonRewards.h` for patterns
- Game state/player types: see `RLGymCPP/Gamestates/` and `CommonValues.h`
- Math utilities: see `RLGymCPP/Math.h` and `Rewards/RewardMath.h`
- Phase config details: see `RewardConfig.cpp` (the source of truth for what's active)

## 6. ⚠️ CRITICAL: File Deletion Safety Protocol

**IMPORTANT: Any attempt to delete, remove, or clean up directories/files REQUIRES explicit human approval.**

### Rules:
1. **BEFORE any deletion attempt**, Claude must:
   - Clearly state which files/directories will be DELETED
   - List the exact paths
   - Explain why deletion is needed
   - **ASK FOR EXPLICIT USER APPROVAL** in plain English
   - Wait for user to confirm before proceeding

2. **If deletion is attempted without approval:**
   - STOP immediately
   - Report what was attempted
   - Ask for confirmation

3. **Critical directories MUST NEVER be deleted without explicit approval:**
   - `build/` — Contains compiled binaries and checkpoints
   - `build/checkpoints/` — Training checkpoints (IRREPLACEABLE)
   - Any directory with checkpoint data
   - `GigaLearnCPP/` — Source code
   - `src/` — Configuration and entrypoint

4. **Safe cleanup operations (with user consent):**
   - Build cache (`CMakeCache.txt`)
   - Build artifacts in a clean build directory
   - Temporary files that are explicitly identified

### Example of CORRECT behavior:
```
Claude: "I need to clean the build directory. This will DELETE:
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build/CMakeCache.txt
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build/CMakeFiles/

Should I proceed? Please confirm explicitly."

User: "Yes, delete the cache files"

Claude: "Confirmed. Deleting specified files now..."
```

### Example of WRONG behavior:
```
Claude: "Cleaning build directory..." (proceeds without asking)
Claude: "rm -rf build" (deletes entire directory without approval)
```

**This protocol exists because:**
- Training checkpoints are irreplaceable
- Deleting `build/` removes training progress
- Accidental deletions can waste weeks of training
- Human oversight prevents catastrophic losses
