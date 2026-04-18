# Bot MMR Tracker & Auto-Queue System

Complete MMR tracking and auto-queue system for RLBot v5. Automatically queues matches and logs results to a persistent MMR tracker with web-based UI.

---

## Features

### 🎯 Match Monitoring
- **Real-time detection** of match completion via RLBot GameTickPacket
- **Automatic result logging** to persistent JSON file
- **Atomic writes** prevent data corruption from concurrent access
- **Draw handling** - skips logging if match ends in a tie

### 📊 MMR Tracker
- **Web-based UI** for viewing bot MMRs across all gamemodes
- **Elo-style MMR system** (starts at 1000, gains/losses vary by opponent skill)
- **Dynamic K-factor** - Large swings for new bots (±50), smaller for experienced (±9)
- **Beautiful charts** - Line graphs showing MMR trends by bot and gamemode
- **Import/Export** - Load match results from RLBot auto-queue

### 🤖 Auto-Queue System
- **Smart opponent matching** - Selects opponents based on closest MMR
- **Solo queue support** - No duplicate bots on same team
- **Mirror match prevention** - Never queues identical teams
- **Gamemode variety** - Random or fixed gamemode selection
- **Configurable** - Full control via JSON config file

---

## Quick Start

### 1. Enable Match Monitoring

The match monitor is **automatically integrated** into `CppPythonAgent.py`. It starts when the agent runs and stops when it retires.

**No additional setup required!** Just run RLBot normally:

```bash
cd GigaLearnBot/rlbot
python -m rlbot.gui
```

Match results will be automatically logged to: `GigaLearnBot/rlbot/mmr_tracker_matches.json`

### 2. View Results in MMR Tracker

Open the MMR tracker web UI:
```
File → C:\Users\nhafe\OneDrive\Desktop\mmr_tracker.html
```

Or in a browser:
```
file:///C:\Users\nhafe\OneDrive\Desktop\mmr_tracker.html
```

#### Import Results

**Option A: Upload File**
1. Click "Import File" button in the "Import from RLBot" section
2. Select `GigaLearnBot/rlbot/mmr_tracker_matches.json`
3. Click "Import File" button

**Option B: Auto-Load**
1. Click "Auto-Load from RLBot" button
2. Browser will attempt to load directly from the file location

### 3. Enable Auto-Queue (Optional)

To automatically queue consecutive matches:

#### A. Configure `queue_config.json`

Edit `GigaLearnBot/rlbot/queue_config.json`:

```json
{
  "auto_queue_enabled": true,
  "matches_to_play": 10,
  "gamemode_selection": "random",
  "available_gamemodes": ["1v1", "2v2", "3v3", "soloq2v2", "soloq3v3"],
  "bot_pool": ["Sekalf", "BotA", "BotB", "BotC"],
  "opponent_matching": {
    "strategy": "closest_mmr",
    "exclude_mirror_matches": true,
    "enforce_mmr_transaction": false,
    "mmr_tolerance": 100
  },
  "mmr_tracker_file": "GigaLearnBot/rlbot/mmr_tracker_matches.json"
}
```

#### B. Start Auto-Queue

Launch the queue system:

```bash
cd GigaLearnBot/rlbot
python match_queue.py
```

Monitor output for queue status and match updates.

---

## Configuration Guide

### `queue_config.json` Options

| Option | Type | Description |
|--------|------|-------------|
| `auto_queue_enabled` | bool | Enable/disable auto-queue |
| `matches_to_play` | int | Number of matches to queue (0 = infinite) |
| `gamemode_selection` | string | `"random"` or `"fixed"` (uses first gamemode) |
| `available_gamemodes` | array | List of gamemodes to randomly select from |
| `bot_pool` | array | All bots available for selection |
| `opponent_matching.strategy` | string | Always `"closest_mmr"` |
| `opponent_matching.exclude_mirror_matches` | bool | Never queue identical teams |
| `opponent_matching.enforce_mmr_transaction` | bool | Skip near-balanced matches (±10 MMR) |
| `opponent_matching.mmr_tolerance` | int | Max MMR diff for "closest match" (±100) |
| `mmr_tracker_file` | string | Path to MMR tracker JSON file |

### Example Configurations

#### Self-Play (Sekalf vs itself)
```json
{
  "auto_queue_enabled": true,
  "matches_to_play": 5,
  "gamemode_selection": "fixed",
  "available_gamemodes": ["2v2"],
  "bot_pool": ["Sekalf"]
}
```
**Note:** With only 1 bot, mirror matches will be skipped.

#### Tournament (5 bots, random gamemode)
```json
{
  "auto_queue_enabled": true,
  "matches_to_play": 20,
  "gamemode_selection": "random",
  "available_gamemodes": ["1v1", "2v2", "3v3", "soloq2v2", "soloq3v3"],
  "bot_pool": ["Sekalf", "BotA", "BotB", "BotC", "BotD"],
  "opponent_matching": {
    "enforce_mmr_transaction": true,
    "mmr_tolerance": 150
  }
}
```

#### Fixed 1v1
```json
{
  "auto_queue_enabled": true,
  "matches_to_play": 10,
  "gamemode_selection": "fixed",
  "available_gamemodes": ["1v1"],
  "bot_pool": ["Sekalf", "BotA", "BotB"]
}
```

---

## Solo Queue Constraints

### What is Solo Queue?

In `soloq2v2` and `soloq3v3`, each bot plays as an individual:
- **soloq2v2**: 2 different bots per team (2v2 mode, but distinct bots)
- **soloq3v3**: 3 different bots per team (3v3 mode, but distinct bots)

### Constraints Applied

**Solo Queue Modes:**
- ❌ **Invalid**: Team1 = ["Sekalf", "Sekalf"] → Duplicate bot
- ✓ **Valid**: Team1 = ["Sekalf", "BotA"] → Different bots

**All Modes:**
- ❌ **Invalid**: Team1 = ["Sekalf", "BotA"] vs Team2 = ["Sekalf", "BotA"] → Mirror match
- ✓ **Valid**: Team1 = ["Sekalf", "BotA"] vs Team2 = ["BotB", "BotC"] → Unique teams

---

## MMR System

### Starting MMR: 1000

All bots start at 1000 MMR in each gamemode.

### K-Factor (Rating Volatility)

**New Bots (0-100 games):**
- ±50 MMR per match
- Large swings to quickly calibrate skill

**Experienced Bots (100+ games):**
- ±9 MMR per match
- Smaller changes for stable rankings

**Transition:** Linear from ±50 to ±9 over first 100 games

### Win/Loss Examples

#### Scenario 1: Beat weaker opponent
- Your MMR: 1200, Opponent: 1000
- Expected: ~62% win chance
- **Win**: +18 MMR (less because favored)
- **Loss**: -32 MMR (more because upset)

#### Scenario 2: Beat equally skilled opponent
- Your MMR: 1200, Opponent: 1200
- Expected: 50% win chance
- **Win**: +50 MMR (high K-factor) or +9 (low K-factor)
- **Loss**: -50 or -9 MMR

#### Scenario 3: Beat much stronger opponent
- Your MMR: 1000, Opponent: 1300
- Expected: ~27% win chance
- **Win**: +73 MMR (huge upset bonus)
- **Loss**: -27 MMR (acceptable loss)

---

## File Structure

```
GigaLearnBot/rlbot/
├── CppPythonAgent.py              (Modified - includes match monitor)
├── match_monitor.py                (NEW - detects matches, logs results)
├── match_queue.py                  (NEW - auto-queue system)
├── mmr_tracker_interface.py         (NEW - JSON I/O layer)
├── rlbot.cfg                       (Participant configuration)
├── queue_config.json               (NEW - auto-queue settings)
├── mmr_tracker_matches.json        (AUTO-CREATED - match history)
├── port.cfg                        (Socket port configuration)
└── MMR_TRACKER_README.md           (This file)

C:\Users\nhafe\OneDrive\Desktop/
└── mmr_tracker.html                (Web UI for viewing MMRs)
```

---

## How It Works

### Match Detection & Logging

1. **Agent starts** → Match monitor thread spawns
2. **RLBot queues match** → CppPythonAgent connects to C++ server
3. **Match plays** → Monitor polls GameTickPacket every ~16ms (60 Hz)
4. **Match ends** → Monitor detects `is_match_ended` flag
5. **Result logged** → mmr_tracker_matches.json updated atomically
6. **Next iteration** → Monitor waits for next match

### Auto-Queue Flow

1. **Queue starts** → Loads config, reads current bot MMRs
2. **Select gamemode** → Random or fixed based on config
3. **Select Team1** → Random N bots from pool (no duplicates if solo queue)
4. **Select Team2** → Closest MMR to Team1 (no mirrors, no duplicates)
5. **Validate** → Check all constraints met
6. **Queue match** → Update rlbot.cfg participants
7. **Wait** → Monitor for match completion
8. **Log result** → MMR tracker updated
9. **Repeat** → Back to step 2 until match limit reached

---

## Troubleshooting

### Matches not logging

**Symptom:** Play matches but nothing appears in `mmr_tracker_matches.json`

**Fixes:**
1. Check RLBot is running with modified `CppPythonAgent.py`
2. Verify `match_monitor.py` and `mmr_tracker_interface.py` are in `rlbot/` directory
3. Check logs for errors:
   - Console output when RLBot starts
   - Look for "Match monitor" messages
4. Manually test:
   ```python
   from mmr_tracker_interface import MMRTrackerInterface
   tracker = MMRTrackerInterface()
   tracker.write_match("2v2", ["Bot1", "Bot2"], ["Bot3", "Bot4"], 3, 1)
   ```

### Auto-queue not starting

**Symptom:** `auto_queue_enabled: true` but queue doesn't start

**Fixes:**
1. Verify `queue_config.json` is valid JSON (check syntax)
2. Ensure `auto_queue_enabled` is exactly `true` (not `"true"`)
3. Check bot pool has enough bots for team size
4. Look for errors in console output

### Incorrect MMR changes

**Symptom:** Bot gained 0 MMR or unexpected amounts

**Causes:**
- Match was logged as a draw → MMR change is 0
- Bot has played many games → K-factor reduced to ±9
- Opponent MMR very close → Small MMR transaction expected

**Check:** View match history in mmr_tracker.html, verify goals are not tied.

### File access errors

**Symptom:** "Failed to write match" or similar errors

**Fixes:**
1. Ensure `GigaLearnBot/rlbot/` is writable
2. Check no other program is locking `mmr_tracker_matches.json`
3. Verify `queue_config.json` has correct file paths
4. Run RLBot with admin privileges if permission denied

---

## API Reference

### Match Monitor

```python
from match_monitor import MatchMonitor

monitor = MatchMonitor()
monitor.start()          # Start background monitoring
monitor.stop()           # Stop monitoring

# Get data
current_mmr = monitor.get_current_mmr("Sekalf", "2v2")
all_bots = monitor.get_all_bots()
mmrs = monitor.get_gamemode_mmrs("1v1")
```

### MMR Tracker Interface

```python
from mmr_tracker_interface import MMRTrackerInterface

tracker = MMRTrackerInterface()

# Write a match result
tracker.write_match(
    gamemode="2v2",
    team1_bots=["Sekalf", "BotA"],
    team2_bots=["BotB", "BotC"],
    team1_goals=3,
    team2_goals=1
)

# Read data
matches = tracker.read_matches()
mmr = tracker.get_bot_mmr("Sekalf", "2v2")
mmrs = tracker.get_gamemode_mmrs("2v2")
bots = tracker.get_all_bots()
```

### Match Queue

```python
from match_queue import MatchQueue

queue = MatchQueue()
queue.start()            # Start auto-queue system
queue.stop()             # Stop auto-queue

status = queue.get_status()  # Get queue status
```

---

## Advanced Usage

### Manual Match Logging

```python
from mmr_tracker_interface import MMRTrackerInterface

tracker = MMRTrackerInterface("path/to/mmr_tracker_matches.json")

# Log a match manually
success = tracker.write_match(
    gamemode="3v3",
    team1_bots=["Sekalf", "BotA", "BotB"],
    team2_bots=["BotC", "BotD", "BotE"],
    team1_goals=5,
    team2_goals=2
)

print(f"Match logged: {success}")
```

### Programmatic Queue Control

```python
from match_queue import MatchQueue
import time

queue = MatchQueue("custom_config.json")

# Queue 5 matches
queue.config["matches_to_play"] = 5
queue.start()

# Monitor progress
while queue.is_running:
    status = queue.get_status()
    print(f"Matches played: {status['matches_played']}")
    time.sleep(10)

print("Queue complete!")
```

---

## Known Limitations

1. **File I/O**: MMR tracker JSON must be read/written atomically. On network drives, this may have delays.
2. **Web Import**: Browser may not allow reading from file paths directly due to security restrictions. Use file upload instead.
3. **Large datasets**: With 1000+ matches, web UI chart rendering may be slow. Consider archiving old matches.
4. **Concurrent matches**: System assumes 1 match at a time. Multiple simultaneous matches not supported.

---

## Support

For issues or questions:

1. **Check logs**: Look at console output when RLBot starts
2. **Verify config**: Ensure `queue_config.json` and `rlbot.cfg` are valid
3. **Test manually**: Try logging a single match via Python API
4. **Review files**: Inspect `mmr_tracker_matches.json` for data integrity

---

## Updates & Version History

**Current Version**: 1.0.0 (April 2026)

### Components
- `match_monitor.py` - v1.0
- `mmr_tracker_interface.py` - v1.0
- `match_queue.py` - v1.0
- `mmr_tracker.html` - v1.1 (with import feature)
