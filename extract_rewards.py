#!/usr/bin/env python3
"""
Extract reward configurations from RewardConfig.cpp and convert to JSON format
for the new data-driven configuration system.
"""

import re
import json
import sys
from pathlib import Path

# Define phase names in order
PHASES = [
    "PHASE_1_BRONZE",
    "PHASE_2_SILVER",
    "PHASE_3_GOLD",
    "PHASE_4_PLATINUM",
    "PHASE_5_DIAMOND",
    "PHASE_6_CHAMPION",
    "PHASE_7_GC",
    "PHASE_8_SSL",
    "PHASE_9_PRO"
]

def find_matching_paren(text, start):
    """Find the position of the closing parenthesis matching the one at start."""
    depth = 1
    i = start + 1
    while i < len(text) and depth > 0:
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
        i += 1
    return i - 1 if depth == 0 else -1

def extract_rewards_from_phase(phase_content):
    """
    Parse a phase content block and extract rewards.
    Returns dict of {reward_name: weight}
    """
    rewards = {}

    # Find all WeightedReward( patterns
    pattern = r'WeightedReward\s*\('

    for match in re.finditer(pattern, phase_content):
        pos = match.start()
        paren_start = phase_content.find('(', match.start())

        if paren_start == -1:
            continue

        # Find matching closing paren
        paren_end = find_matching_paren(phase_content, paren_start)
        if paren_end == -1:
            continue

        # Extract the content inside WeightedReward(...)
        content = phase_content[paren_start + 1:paren_end]

        # Find the last comma (separates reward from weight)
        # We need to be careful because there might be commas inside nested parens
        last_comma = -1
        depth = 0
        for i in range(len(content) - 1, -1, -1):
            if content[i] == ')':
                depth += 1
            elif content[i] == '(':
                depth -= 1
            elif content[i] == ',' and depth == 0:
                last_comma = i
                break

        if last_comma == -1:
            continue

        # Extract weight (right side of the comma)
        weight_str = content[last_comma + 1:].strip().rstrip('f')
        try:
            weight = float(weight_str)
        except ValueError:
            continue

        # Extract reward part (left side of the comma)
        reward_part = content[:last_comma].strip()

        # Extract reward name from "new RewardName(...)"
        match_new = re.search(r'new\s+([A-Za-z0-9_]+)\s*\(', reward_part)
        if not match_new:
            continue

        reward_name = match_new.group(1)

        # Skip ZeroSumReward wrapper
        if reward_name == "ZeroSumReward":
            # Extract the inner reward if it exists
            inner_match = re.search(r'new\s+([A-Za-z0-9_]+)\s*\(', reward_part[match_new.end():])
            if inner_match:
                reward_name = inner_match.group(1)
            else:
                continue

        # Avoid duplicates - use the highest weight if we see it multiple times
        if reward_name not in rewards or weight > rewards[reward_name]:
            rewards[reward_name] = weight

    return rewards

def extract_rewards_from_file(filepath):
    """
    Parse RewardConfig.cpp and extract rewards for each phase.
    Returns a dict: {phase_name: {reward_name: weight}}

    Note: Phases 7 and 8 fall through to phase 9, so they share the same configuration.
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    result = {}
    phase_9_rewards = None

    for phase in PHASES:
        # Special handling for phases 7-9 which are combined in the switch statement
        if phase in ["PHASE_7_GC", "PHASE_8_SSL"]:
            # These will be filled in from PHASE_9_PRO later
            continue

        # Find the case statement for this phase
        pattern = r'case\s+TrainingPhase::' + phase + r'\s*:(.*?)(?=case|default:|}};)'
        match = re.search(pattern, content, re.DOTALL)

        if not match:
            print(f"Warning: Phase {phase} not found", file=sys.stderr)
            result[phase] = {}
            continue

        phase_content = match.group(1)
        rewards = extract_rewards_from_phase(phase_content)
        result[phase] = rewards

        # Store phase 9 rewards for use with phases 7-8
        if phase == "PHASE_9_PRO":
            phase_9_rewards = rewards

    # Phases 7 and 8 use the same configuration as phase 9 (they fall through in the switch)
    if phase_9_rewards:
        result["PHASE_7_GC"] = phase_9_rewards.copy()
        result["PHASE_8_SSL"] = phase_9_rewards.copy()

    return result

def main():
    reward_config_path = Path(r"C:\Projects\RLGymBot_v3\GigaLearnBot\src\RewardConfig.cpp")

    if not reward_config_path.exists():
        print(f"Error: {reward_config_path} not found", file=sys.stderr)
        sys.exit(1)

    print(f"Extracting rewards from {reward_config_path}...", file=sys.stderr)

    phases_config = extract_rewards_from_file(str(reward_config_path))

    # Format as JSON
    output = {"phases": phases_config}

    # Write to file
    output_path = Path(r"C:\Projects\RLGymBot_v3\GigaLearnBot\reward_phases.json")

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(output, f, indent=2)

    print(f"Successfully extracted rewards to {output_path}", file=sys.stderr)

    # Print summary
    total_rewards = sum(len(rewards) for rewards in phases_config.values())
    print(f"\nSummary:", file=sys.stderr)
    for phase, rewards in phases_config.items():
        print(f"  {phase}: {len(rewards)} rewards", file=sys.stderr)
    print(f"  Total: {total_rewards} reward instances across all phases", file=sys.stderr)

    # Print the JSON
    print(json.dumps(output, indent=2))

if __name__ == "__main__":
    main()
