"""
Match Queue - Auto-Queue System for RLBot v5
Automatically queues consecutive matches with smart opponent matching based on MMR.
Supports multiple gamemodes, solo queue constraints, and mirror match prevention.
"""

import json
import os
import logging
import random
import threading
import time
from typing import List, Dict, Optional, Tuple
from configparser import ConfigParser

from mmr_tracker_interface import MMRTrackerInterface

logger = logging.getLogger("MatchQueue")


class MatchQueue:
    """
    Automatically queues matches with intelligent opponent selection.
    Features:
    - Random or fixed gamemode selection
    - Solo queue constraints (no duplicate bots on same team)
    - Mirror match prevention
    - MMR-based opponent matching
    - Optional MMR transaction enforcement
    """

    def __init__(self, config_path: Optional[str] = None):
        """
        Initialize the match queue.

        Args:
            config_path: Path to queue_config.json
                        Defaults to GigaLearnBot/rlbot/queue_config.json
        """
        if config_path is None:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            config_path = os.path.join(script_dir, "queue_config.json")

        self.config_path = config_path
        self.logger = logging.getLogger("MatchQueue")
        self.config = self._load_config()
        self.tracker = MMRTrackerInterface()

        # State tracking
        self.matches_played = 0
        self.is_running = False

    def _load_config(self) -> Dict:
        """Load configuration from queue_config.json"""
        default_config = {
            "auto_queue_enabled": False,
            "matches_to_play": 0,  # 0 = infinite
            "gamemode_selection": "fixed",
            "available_gamemodes": ["1v1", "2v2", "3v3", "soloq2v2", "soloq3v3"],
            "bot_pool": ["Bot1", "Bot2", "Bot3"],
            "opponent_matching": {
                "strategy": "closest_mmr",
                "exclude_mirror_matches": True,
                "enforce_mmr_transaction": False,
                "mmr_tolerance": 100
            },
            "mmr_tracker_file": "GigaLearnBot/rlbot/mmr_tracker_matches.json"
        }

        try:
            if not os.path.exists(self.config_path):
                self.logger.info(f"Config not found, creating default at {self.config_path}")
                self._save_config(default_config)
                return default_config

            with open(self.config_path, 'r') as f:
                config = json.load(f)
                self.logger.info(f"Loaded config from {self.config_path}")
                return config

        except Exception as e:
            self.logger.error(f"Failed to load config: {e}, using defaults")
            return default_config

    def _save_config(self, config: Dict) -> None:
        """Save configuration to file"""
        try:
            os.makedirs(os.path.dirname(self.config_path), exist_ok=True)
            with open(self.config_path, 'w') as f:
                json.dump(config, f, indent=2)
            self.logger.info(f"Saved config to {self.config_path}")
        except Exception as e:
            self.logger.error(f"Failed to save config: {e}")

    def start(self) -> None:
        """Start the auto-queue system"""
        if not self.config.get("auto_queue_enabled"):
            self.logger.info("Auto-queue is disabled in config")
            return

        if self.is_running:
            self.logger.warning("Queue is already running")
            return

        self.is_running = True
        self.matches_played = 0
        self.logger.info("Match queue started")

        # Run in background thread
        queue_thread = threading.Thread(target=self._queue_loop, daemon=True)
        queue_thread.start()

    def stop(self) -> None:
        """Stop the auto-queue system"""
        self.is_running = False
        self.logger.info("Match queue stopped")

    def _queue_loop(self) -> None:
        """Main queue loop (runs in background thread)"""
        while self.is_running:
            try:
                # Check if we've reached the match limit
                matches_limit = self.config.get("matches_to_play", 0)
                if matches_limit > 0 and self.matches_played >= matches_limit:
                    self.logger.info(f"Reached match limit ({matches_limit}), stopping")
                    self.is_running = False
                    break

                # Select and queue next match
                gamemode = self._select_gamemode()
                team1_bots = self._select_team1(gamemode)
                team2_bots = self._select_team2(gamemode, team1_bots)

                # Validate match
                if self._validate_match(gamemode, team1_bots, team2_bots):
                    self._queue_match(gamemode, team1_bots, team2_bots)
                    self.matches_played += 1
                    self.logger.info(
                        f"Queued match {self.matches_played}: {gamemode} - "
                        f"{team1_bots} vs {team2_bots}"
                    )
                else:
                    self.logger.warning("Match validation failed, retrying...")
                    time.sleep(1)
                    continue

                # Wait for match completion
                self._wait_for_match_completion(gamemode, team1_bots, team2_bots)

                # Small delay before queuing next match
                time.sleep(2)

            except Exception as e:
                self.logger.error(f"Error in queue loop: {e}")
                time.sleep(5)

    def _select_gamemode(self) -> str:
        """Select a gamemode based on configuration"""
        selection_type = self.config.get("gamemode_selection", "fixed")
        available_modes = self.config.get("available_gamemodes", ["1v1", "2v2", "3v3"])

        if selection_type == "random":
            mode = random.choice(available_modes)
        else:
            # Default to first available gamemode
            mode = available_modes[0] if available_modes else "1v1"

        return mode

    def _select_team1(self, gamemode: str) -> List[str]:
        """Randomly select team 1 with solo queue constraints"""
        team_size = self._get_team_size(gamemode)
        bot_pool = self.config.get("bot_pool", [])
        is_solo_queue = gamemode.startswith("soloq")

        max_retries = 10
        for attempt in range(max_retries):
            # Randomly select bots
            if len(bot_pool) < team_size:
                self.logger.error(f"Bot pool ({len(bot_pool)}) smaller than team size ({team_size})")
                raise ValueError("Insufficient bots in pool for team size")

            team1 = random.sample(bot_pool, team_size)

            # Check for duplicates if solo queue
            if is_solo_queue:
                if len(set(team1)) == len(team1):
                    return team1
                # Retry if duplicates found
            else:
                return team1

        raise ValueError(f"Failed to select team1 after {max_retries} attempts")

    def _select_team2(self, gamemode: str, team1_bots: List[str]) -> List[str]:
        """Select team 2 via closest MMR matching with constraints"""
        team_size = self._get_team_size(gamemode)
        bot_pool = self.config.get("bot_pool", [])
        mmr_tolerance = self.config.get("opponent_matching", {}).get("mmr_tolerance", 100)
        enforce_mmr_transaction = self.config.get("opponent_matching", {}).get(
            "enforce_mmr_transaction", False
        )
        is_solo_queue = gamemode.startswith("soloq")

        # Get current MMRs for all bots
        mmrs = self.tracker.get_gamemode_mmrs(gamemode)

        # Calculate team1 average MMR
        team1_mmrs = [mmrs.get(bot, 1000) for bot in team1_bots]
        team1_avg_mmr = sum(team1_mmrs) / len(team1_mmrs)

        # Score and rank bots for team2
        candidates = []
        for bot in bot_pool:
            if bot in team1_bots:
                continue  # Skip bots already in team1

            bot_mmr = mmrs.get(bot, 1000)
            mmr_diff = abs(bot_mmr - team1_avg_mmr)
            candidates.append((bot, bot_mmr, mmr_diff))

        # Sort by MMR difference (closest first)
        candidates.sort(key=lambda x: x[2])

        if not candidates:
            raise ValueError("No candidates available for team2")

        # Select top N candidates
        max_attempts = 20
        for attempt in range(max_attempts):
            # Select from top candidates (with some randomness)
            top_candidates = candidates[:min(team_size * 3, len(candidates))]
            team2 = [random.choice(top_candidates)[0] for _ in range(team_size)]

            # Check for duplicates if solo queue
            if is_solo_queue:
                if len(set(team2)) != len(team2):
                    continue  # Retry if duplicates

            # Check for mirror match
            if set(team1_bots) == set(team2):
                continue  # Retry if mirror

            # Check MMR transaction value
            if enforce_mmr_transaction:
                team2_mmrs = [mmrs.get(bot, 1000) for bot in team2]
                team2_avg_mmr = sum(team2_mmrs) / len(team2_mmrs)

                if abs(team2_avg_mmr - team1_avg_mmr) < 10:  # < 10 MMR difference = low value
                    self.logger.debug(
                        f"Low MMR transaction ({team1_avg_mmr} vs {team2_avg_mmr}), retrying"
                    )
                    continue

            return team2

        raise ValueError("Failed to select valid team2")

    def _get_team_size(self, gamemode: str) -> int:
        """Get team size for a gamemode"""
        sizes = {
            "1v1": 1,
            "2v2": 2,
            "soloq2v2": 2,
            "3v3": 3,
            "soloq3v3": 3
        }
        return sizes.get(gamemode, 1)

    def _validate_match(self, gamemode: str, team1_bots: List[str], team2_bots: List[str]) -> bool:
        """Validate that a match meets all requirements"""
        try:
            # Check not mirror match
            if set(team1_bots) == set(team2_bots):
                self.logger.debug("Mirror match detected, invalid")
                return False

            # Check no single-bot dominance in solo queue
            if gamemode.startswith("soloq"):
                if len(set(team1_bots)) != len(team1_bots):
                    self.logger.debug("Team1 has duplicates in solo queue, invalid")
                    return False
                if len(set(team2_bots)) != len(team2_bots):
                    self.logger.debug("Team2 has duplicates in solo queue, invalid")
                    return False

            # Check bot pool contains all selected bots
            bot_pool = self.config.get("bot_pool", [])
            for bot in team1_bots + team2_bots:
                if bot not in bot_pool:
                    self.logger.warning(f"Bot {bot} not in bot pool")
                    return False

            return True

        except Exception as e:
            self.logger.error(f"Validation error: {e}")
            return False

    def _queue_match(self, gamemode: str, team1_bots: List[str], team2_bots: List[str]) -> None:
        """Queue a match in RLBot by modifying rlbot.cfg"""
        try:
            config_path = os.path.join(
                os.path.dirname(__file__),
                "rlbot.cfg"
            )

            parser = ConfigParser()
            parser.read(config_path)

            # Update participant configuration
            participants = team1_bots + team2_bots
            for idx, bot_name in enumerate(participants):
                team_idx = 0 if idx < len(team1_bots) else 1
                key = f"participant_team_{idx}"
                name_key = f"participant_bot_name_{idx}"

                if "Match Setup" not in parser:
                    parser["Match Setup"] = {}

                parser["Match Setup"][key] = str(team_idx)
                parser["Match Setup"][name_key] = bot_name

                self.logger.debug(f"Set participant {idx}: {bot_name} (team {team_idx})")

            # Write updated config
            with open(config_path, 'w') as f:
                parser.write(f)

            self.logger.info(f"Updated rlbot.cfg for match: {gamemode}")

        except Exception as e:
            self.logger.error(f"Failed to queue match in RLBot: {e}")

    def _wait_for_match_completion(
        self, gamemode: str, team1_bots: List[str], team2_bots: List[str]
    ) -> None:
        """Wait for the current match to complete"""
        timeout_seconds = 600  # 10 minute timeout
        check_interval = 5  # Check every 5 seconds

        start_time = time.time()
        last_match_count = len(self.tracker.read_matches())

        while time.time() - start_time < timeout_seconds:
            try:
                current_matches = self.tracker.read_matches()
                current_match_count = len(current_matches)

                # Check if a new match was logged
                if current_match_count > last_match_count:
                    self.logger.info("Match completed and logged")
                    return

                time.sleep(check_interval)

            except Exception as e:
                self.logger.warning(f"Error checking match completion: {e}")
                time.sleep(check_interval)

        self.logger.warning(f"Match timeout ({timeout_seconds}s) reached")

    def get_status(self) -> Dict:
        """Get current queue status"""
        matches_limit = self.config.get("matches_to_play", 0)
        return {
            "is_running": self.is_running,
            "matches_played": self.matches_played,
            "matches_limit": matches_limit if matches_limit > 0 else "infinite",
            "auto_queue_enabled": self.config.get("auto_queue_enabled", False),
            "available_gamemodes": self.config.get("available_gamemodes", []),
            "bot_pool_size": len(self.config.get("bot_pool", []))
        }


# Standalone launcher script
if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )

    queue = MatchQueue()

    if queue.config.get("auto_queue_enabled"):
        print("Starting auto-queue system...")
        print(f"Status: {queue.get_status()}")
        queue.start()

        try:
            while True:
                time.sleep(10)
                print(f"Status: {queue.get_status()}")
        except KeyboardInterrupt:
            print("\nShutting down...")
            queue.stop()
    else:
        print("Auto-queue is disabled in config")
