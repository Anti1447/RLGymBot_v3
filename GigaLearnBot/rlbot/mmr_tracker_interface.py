"""
MMR Tracker Interface - JSON I/O Layer
Handles atomic reads/writes to mmr_tracker_matches.json with proper synchronization.
"""

import json
import os
import tempfile
import threading
from typing import Dict, List, Optional, Tuple
from datetime import datetime
import logging

logger = logging.getLogger("MMRTracker")


class MMRTrackerInterface:
    """
    Interface for reading/writing match results to the MMR tracker JSON file.
    Handles file locking and atomic writes to prevent corruption.
    """

    def __init__(self, tracker_file_path: str = None):
        """
        Initialize the MMR tracker interface.

        Args:
            tracker_file_path: Path to mmr_tracker_matches.json
                              Defaults to GigaLearnBot/rlbot/mmr_tracker_matches.json
        """
        if tracker_file_path is None:
            # Construct default path relative to this script
            script_dir = os.path.dirname(os.path.abspath(__file__))
            tracker_file_path = os.path.join(script_dir, "mmr_tracker_matches.json")

        self.tracker_file_path = tracker_file_path
        self.lock = threading.RLock()
        self.logger = logging.getLogger("MMRTrackerInterface")

        # Initialize file if it doesn't exist
        if not os.path.exists(self.tracker_file_path):
            self._initialize_file()

    def _initialize_file(self) -> None:
        """Create an empty tracker file with proper structure."""
        try:
            with self.lock:
                initial_data = {"matches": []}
                with open(self.tracker_file_path, 'w') as f:
                    json.dump(initial_data, f, indent=2)
                self.logger.info(f"Initialized MMR tracker file: {self.tracker_file_path}")
        except Exception as e:
            self.logger.error(f"Failed to initialize tracker file: {e}")

    def read_matches(self) -> List[Dict]:
        """
        Read all recorded matches from the tracker file.

        Returns:
            List of match dictionaries, ordered by timestamp (newest last)
        """
        try:
            with self.lock:
                with open(self.tracker_file_path, 'r') as f:
                    data = json.load(f)
                    return data.get("matches", [])
        except json.JSONDecodeError:
            self.logger.error("Failed to decode MMR tracker JSON")
            return []
        except Exception as e:
            self.logger.error(f"Failed to read tracker file: {e}")
            return []

    def get_bot_mmr(self, bot_name: str, gamemode: str) -> float:
        """
        Calculate current MMR for a bot in a specific gamemode.

        Args:
            bot_name: Name of the bot
            gamemode: Gamemode (1v1, 2v2, 3v3, soloq2v2, soloq3v3)

        Returns:
            Current MMR (1000 if bot has no history in this gamemode)
        """
        matches = self.read_matches()
        mmr = 1000.0  # Starting MMR

        for match in matches:
            if match.get("gamemode") != gamemode:
                continue

            # Check if bot is in team1 (winner)
            team1_bots = match.get("team1", {}).get("bots", [])
            if bot_name in team1_bots:
                # Find this bot's MMR change
                # We need to calculate it from the match data
                mmr_change = self._calculate_mmr_change_for_bot(
                    bot_name, match, is_winner=True, gamemode=gamemode
                )
                mmr += mmr_change
                continue

            # Check if bot is in team2 (loser)
            team2_bots = match.get("team2", {}).get("bots", [])
            if bot_name in team2_bots:
                mmr_change = self._calculate_mmr_change_for_bot(
                    bot_name, match, is_winner=False, gamemode=gamemode
                )
                mmr += mmr_change

        return mmr

    def _calculate_mmr_change_for_bot(
        self, bot_name: str, match: Dict, is_winner: bool, gamemode: str
    ) -> float:
        """
        Calculate MMR change for a specific bot in a match.
        Uses the same Elo formula as the HTML tracker.
        """
        team1_bots = match.get("team1", {}).get("bots", [])
        team2_bots = match.get("team2", {}).get("bots", [])

        # Get team MMRs at the time of match
        # For simplicity, we'll recalculate using current state
        # (In a real system, you'd store MMR at match time)
        team1_avg_mmr = sum(self.get_bot_mmr(b, gamemode) for b in team1_bots) / len(team1_bots)
        team2_avg_mmr = sum(self.get_bot_mmr(b, gamemode) for b in team2_bots) / len(team2_bots)

        # Get bot's current MMR
        bot_mmr = self.get_bot_mmr(bot_name, gamemode)
        if is_winner:
            opponent_avg_mmr = team2_avg_mmr
        else:
            opponent_avg_mmr = team1_avg_mmr

        # K-factor calculation (same as HTML tracker)
        games = self._count_games_for_bot(bot_name, gamemode)
        max_games = 100
        k_when_new = 50
        k_when_experienced = 9
        k = k_when_new - ((k_when_new - k_when_experienced) * min(games, max_games) / max_games)

        # Expected score
        expected = 1 / (1 + 10 ** ((opponent_avg_mmr - bot_mmr) / 400))

        # Actual score
        actual = 1 if is_winner else 0

        # Change
        change = k * (actual - expected)
        return change

    def _count_games_for_bot(self, bot_name: str, gamemode: str) -> int:
        """Count how many games a bot has played in a gamemode."""
        matches = self.read_matches()
        count = 0

        for match in matches:
            if match.get("gamemode") != gamemode:
                continue

            team1_bots = match.get("team1", {}).get("bots", [])
            team2_bots = match.get("team2", {}).get("bots", [])

            if bot_name in team1_bots or bot_name in team2_bots:
                count += 1

        return count

    def write_match(
        self,
        gamemode: str,
        team1_bots: List[str],
        team2_bots: List[str],
        team1_goals: int,
        team2_goals: int,
        timestamp: Optional[str] = None
    ) -> bool:
        """
        Write a completed match to the tracker file.

        Args:
            gamemode: Match gamemode (1v1, 2v2, 3v3, soloq2v2, soloq3v3)
            team1_bots: List of bot names on team 1 (winner)
            team2_bots: List of bot names on team 2 (loser)
            team1_goals: Goals scored by team 1
            team2_goals: Goals scored by team 2
            timestamp: ISO timestamp (auto-generated if None)

        Returns:
            True if successful, False otherwise
        """
        if timestamp is None:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Validate input
        if not gamemode or not team1_bots or not team2_bots:
            self.logger.error("Invalid match data: missing gamemode or bots")
            return False

        if team1_goals == team2_goals:
            self.logger.warning("Match is a draw, skipping (no winner)")
            return False

        if team1_goals < team2_goals:
            self.logger.warning("Team 1 should be the winner, swapping teams")
            team1_bots, team2_bots = team2_bots, team1_bots
            team1_goals, team2_goals = team2_goals, team1_goals

        match_data = {
            "gamemode": gamemode,
            "timestamp": timestamp,
            "team1": {
                "bots": team1_bots,
                "goals": team1_goals
            },
            "team2": {
                "bots": team2_bots,
                "goals": team2_goals
            }
        }

        return self._write_match_atomic(match_data)

    def _write_match_atomic(self, match_data: Dict) -> bool:
        """
        Write match data to file atomically (write to temp, then rename).
        """
        try:
            with self.lock:
                # Read current data
                try:
                    with open(self.tracker_file_path, 'r') as f:
                        data = json.load(f)
                except (json.JSONDecodeError, FileNotFoundError):
                    data = {"matches": []}

                # Add new match
                if "matches" not in data:
                    data["matches"] = []
                data["matches"].append(match_data)

                # Write to temp file
                temp_fd, temp_path = tempfile.mkstemp(
                    suffix='.json',
                    dir=os.path.dirname(self.tracker_file_path)
                )

                try:
                    with os.fdopen(temp_fd, 'w') as f:
                        json.dump(data, f, indent=2)

                    # Rename temp to target (atomic on most systems)
                    os.replace(temp_path, self.tracker_file_path)
                    self.logger.info(f"Logged match: {match_data['gamemode']} - {match_data['timestamp']}")
                    return True

                except Exception as e:
                    # Clean up temp file
                    try:
                        os.unlink(temp_path)
                    except:
                        pass
                    raise e

        except Exception as e:
            self.logger.error(f"Failed to write match: {e}")
            return False

    def get_all_bots(self) -> List[str]:
        """
        Get a list of all bots that have played matches.
        """
        matches = self.read_matches()
        bots = set()

        for match in matches:
            team1_bots = match.get("team1", {}).get("bots", [])
            team2_bots = match.get("team2", {}).get("bots", [])
            bots.update(team1_bots)
            bots.update(team2_bots)

        return sorted(list(bots))

    def get_gamemode_mmrs(self, gamemode: str) -> Dict[str, float]:
        """
        Get current MMRs for all bots in a specific gamemode.

        Returns:
            Dictionary mapping bot_name -> current_mmr
        """
        bots = self.get_all_bots()
        mmrs = {}

        for bot in bots:
            mmrs[bot] = self.get_bot_mmr(bot, gamemode)

        return mmrs
