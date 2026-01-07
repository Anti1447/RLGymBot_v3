# Advanced Rocket League Mechanics Reward System

A comprehensive reward system for training reinforcement learning agents in Rocket League, focusing on advanced mechanics that are crucial for competitive play.

## 🎯 Overview

This system implements rewards for four critical advanced mechanics:

1. **Double Tap** - Two-touch aerial sequences with surface bounces (backboard/wall/ceiling)
2. **Boost Stealing** - Taking opponent's 100-boost pads for resource denial
3. **Reverse Aerials** - Hitting the ball while facing away (advanced car control)
4. **Field Spacing** - Proper team positioning with coverage cone overlap detection

## 📦 Files Included

### Core Implementation
- **`advanced_mechanics_rewards.py`** - Main reward calculation system
  - `AdvancedMechanicsRewards` class with all mechanic implementations
  - Touch tracking for double tap detection
  - Surface contact detection (backboard/wall/ceiling)
  - Coverage cone system for spacing analysis

### Testing
- **`test_advanced_mechanics.py`** - Comprehensive test suite
  - Tests for all mechanics
  - Edge case validation
  - Integration tests
  - Run with: `pytest test_advanced_mechanics.py -v`

### Documentation
- **`ADVANCED_MECHANICS_DOCUMENTATION.md`** - Complete guide
  - Detailed mechanic explanations
  - Reward structures and formulas
  - Integration patterns
  - Tuning recommendations
  - Common issues and solutions
  - Hyperparameter reference

### Visualization
- **`mechanics_visualizer.py`** - Debugging and analysis tools
  - Field spacing visualization with coverage cones
  - Reward history plots with moving averages
  - Distribution analysis (violin plots)
  - Correlation matrices
  - Double tap sequence visualization

### Examples
- **`usage_examples.py`** - Integration patterns
  - Environment wrapper example
  - Curriculum learning implementation
  - Multi-agent credit assignment
  - Reward shaping with potential functions
  - Priority-based reward combination
  - Logging utilities
  - Complete training loop example

## 🚀 Quick Start

```python
from advanced_mechanics_rewards import AdvancedMechanicsRewards, Touch
import numpy as np

# Initialize
reward_calc = AdvancedMechanicsRewards()

# Example: Calculate spacing reward
game_state = {
    'player_positions': [
        np.array([0, -2000, 17]),
        np.array([1000, -1000, 17]),
    ],
    'player_rotations': [
        np.array([0, np.pi/2, 0]),
        np.array([0, np.pi/4, 0]),
    ],
    'player_velocities': [np.array([0, 0, 0]), np.array([0, 0, 0])],
    'team_indices': [0, 0],
    'ball_pos': np.array([500, 0, 100]),
    'is_airborne': [False, False],
    'ball_touched': False,
    'boost_collected': False,
}

# Calculate all rewards for player 0
rewards = reward_calc.calculate_all_rewards(game_state, player_id=0)
print(rewards)
# Output: {'double_tap': 0.0, 'boost_steal': 0.0, 'reverse_aerial': 0.0, 'spacing': 0.04}
```

## 🎮 Mechanics Details

### Double Tap (0.3 - 4.5 reward range)
- Base: 0.3 for execution
- Surface multipliers: Backboard 1.5x, Wall 1.2x, Ceiling 1.8x
- Height bonus: +50% (based on altitude)
- Speed bonus: +30% (based on ball velocity)
- Goal bonus: +3.0 (massive spike)

**Key Feature**: Automatic surface detection using ball position and velocity changes

### Boost Steal (0.15 - 0.5 reward range)
- Base: 0.15 for opponent's boost
- Depth bonus: +0.2 (deeper penetration)
- Corner bonus: +0.15 (back corner pads)

**Strategic Value**: Denies opponents resources and creates offensive pressure

### Reverse Aerial (0.25 - 0.475 reward range)
- Base: 0.25 for execution
- Height bonus: +30% (300-1500 units)
- Backwards bonus: +40% (facing direction)
- Speed bonus: +20% (up to 2000 uu/s)

**Detection**: Uses dot product of player forward direction and ball direction

### Field Spacing (-0.4 to 0.05 reward range)
- Good spacing (overlap < 0.2): +0.05 reward
- Slight overlap (0.2-0.5): -0.1 punishment
- Severe overlap (> 0.5): -0.2 to -0.4 punishment

**System**: Coverage cones with 60° angle, considers distance, direction, and alignment

## 📊 Visualization Examples

```python
from mechanics_visualizer import MechanicsVisualizer

visualizer = MechanicsVisualizer()

# Visualize field spacing with coverage cones
fig, ax = visualizer.visualize_field_spacing(
    player_positions,
    player_rotations,
    ball_pos,
    team_indices,
    overlap_scores,
    save_path='spacing.png'
)

# Plot reward history with moving averages
fig, axes = visualizer.plot_reward_history(
    reward_history,
    window_size=100,
    save_path='history.png'
)
```

## 🔧 Integration Patterns

### Basic Environment Wrapper
```python
class RLEnvWrapper:
    def __init__(self, base_env):
        self.env = base_env
        self.reward_calc = AdvancedMechanicsRewards()
        self.weights = {
            'double_tap': 1.0,
            'boost_steal': 0.8,
            'reverse_aerial': 1.0,
            'spacing': 1.5,
        }
    
    def step(self, actions):
        obs, base_rewards, dones, info = self.env.step(actions)
        game_state = self._extract_game_state(obs, info)
        
        # Add advanced mechanics rewards
        for agent_id in range(len(actions)):
            adv_r = self.reward_calc.calculate_all_rewards(game_state, agent_id)
            weighted_reward = sum(adv_r[k] * self.weights[k] for k in adv_r)
            base_rewards[agent_id] += weighted_reward
        
        return obs, base_rewards, dones, info
```

### Curriculum Learning
```python
from usage_examples import CurriculumMechanicsRewards

curriculum = CurriculumMechanicsRewards()

# Automatically progresses through stages:
# 1. Basic Positioning (spacing only)
# 2. Resource Management (+ boost stealing)
# 3. Advanced Aerials (+ reverse aerials)
# 4. Master Level (all mechanics)

reward, components = curriculum.calculate_rewards(game_state, player_id)
curriculum.on_episode_end()  # Update stage progress
```

## 🎯 Reward Combination Strategies

### Multiplicative Bonuses
Reward agents for executing multiple mechanics together:
```python
if active_mechanics >= 2:
    total_reward *= 1.5  # 50% bonus
```

### Contextual Scaling
Scale rewards based on game situation:
```python
# Boost offensive rewards when losing
if score_diff < 0:
    rewards['double_tap'] *= 1.5
```

### Progressive Difficulty
Reduce rewards as agents master mechanics:
```python
scaling_factor = 1.0 - min(episode / 10000, 0.5)
```

## 📈 Performance Optimization

- **Touch History**: O(1) append, automatic pruning (max 100 touches, 3s window)
- **Cone Caching**: Cache coverage calculations for multiple teammates
- **Batch Processing**: Process multiple agents simultaneously

## ⚙️ Hyperparameter Tuning

| Parameter | Default | Description |
|-----------|---------|-------------|
| `double_tap_window` | 3.0s | Time window for detecting sequences |
| `cone_angle` | 60° | Coverage cone for spacing |
| `cone_depth` | 3000 | Effective depth of coverage |
| `max_touch_history` | 100 | Maximum touches tracked |

### Recommended Weights
- **Double Tap**: 1.0x (large spikes on goals)
- **Boost Steal**: 0.8x (consistent small rewards)
- **Reverse Aerial**: 1.0x (skill demonstration)
- **Spacing**: 1.5x (critical for team play)

## 🐛 Common Issues

### Double Taps Not Detecting
- Verify touch history is updated each frame
- Check `double_tap_window` duration (increase if needed)
- Ensure ball position changes are significant enough

### Boost Steal Not Working
- Verify boost_pad_location is passed correctly
- Check team indices (0=blue, 1=orange)
- Confirm y-coordinate threshold matches field scale

### Spacing Punishments Too Harsh
- Reduce punishment multipliers
- Increase overlap thresholds
- Adjust cone angle (narrower = less overlap)

See `ADVANCED_MECHANICS_DOCUMENTATION.md` for detailed solutions.

## 🧪 Testing

Run the comprehensive test suite:
```bash
pytest test_advanced_mechanics.py -v
```

Tests cover:
- All mechanics (double tap, boost steal, reverse aerial, spacing)
- Edge cases (wrong player, out of bounds, etc.)
- Surface detection accuracy
- Cone overlap calculations
- Integration scenarios

## 📚 Documentation Structure

1. **README.md** (this file) - Quick start and overview
2. **ADVANCED_MECHANICS_DOCUMENTATION.md** - Complete technical reference
3. **Code comments** - Inline documentation in all Python files
4. **usage_examples.py** - Practical integration patterns

## 🎓 Learning Path

1. **Start with spacing** - Fundamental for team play
2. **Add boost management** - Resource control
3. **Introduce aerials** - Mechanical skill
4. **Master double taps** - Complex sequences

Use curriculum learning to gradually introduce complexity!

## 🔍 Debugging Tools

```python
# Enable debug logging
import logging
logging.basicConfig(level=logging.DEBUG)

# Visualize reward components
visualizer = MechanicsVisualizer()
visualizer.plot_reward_distribution(reward_history)

# Log detailed game states
logger = RewardLogger()
logger.log_step(timestep, game_state, rewards)
```

## 🤝 Contributing

Areas for improvement:
- Additional mechanics (flip resets, air dribbles, wave dashes)
- Better surface detection algorithms
- Dynamic reward scaling based on opponent skill
- Multi-modal visualization tools
- Integration with popular RL libraries (RLGym, etc.)

## 📄 License

MIT License - Free to use and modify for RL training needs!

## 🔗 Related Work

This system is designed to complement:
- Ball proximity rewards
- Goal scoring rewards
- Touch quality rewards
- Boost collection rewards
- Defensive positioning rewards

Combine all components for a comprehensive reward structure!

## 📞 Support

For issues, questions, or suggestions:
1. Check `ADVANCED_MECHANICS_DOCUMENTATION.md` for detailed explanations
2. Review `usage_examples.py` for integration patterns
3. Run tests to verify your setup
4. Use visualization tools for debugging

---

**Built for competitive Rocket League RL training** 🚗⚽🎮
