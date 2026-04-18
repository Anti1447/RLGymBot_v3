"""
Match Monitor - RLBot v5 Integration
Monitors game state in real-time and logs completed matches to MMR tracker.
Runs as a background thread alongside the RLBot agent.
"""

import threading
import time
import logging
from typing import Optional, Dict, List
from configparser import ConfigParser
import os

from rlbot.utils.structures import game_interface
from mmr_tracker_interface import MMRTrackerInterface

logger = logging.getLogger("MatchMonitor")


class MatchMonitor:
    """
    Monitors RLBot game state and logs completed matches.
    Runs as a daemon thread to avoid blocking the main agent.
    """

    def __init__(self, tracker_interface: Optional[MMRTrackerInterface] = None):
        """
        Initialize the match monitor.

        Args:
            tracker_interface: MMRTrackerInterface instance (auto-created if None)
        """
        self.tracker = tracker_interface or MMRTrackerInterface()
        self.logger = logging.getLogger("MatchMonitor")

        # State tracking
        self.last_match_ended = False
        self.match_in_progress = False
        self.last_frame_num = 0
        self.last_goal_time = 0

        # Bot name mapping (index -> bot_name)
        self.bot_name_map: Dict[int, str] = {}
        self.load_rlbot_config()

        # Threading
        self.monitor_thread = None
        self.should_stop = False

    def load_rlbot_config(self) -> None:
        """Load bot names from rlbot.cfg"""
        try:
            config_path = os.path.join(
                os.path.dirname(__file__),
                "rlbot.cfg"
            )

            if not os.path.exists(config_path):
                self.logger.warning(f"rlbot.cfg not found at {config_path}")
                return

            parser = ConfigParser()
            parser.read(config_path)

            # Parse participant configurations
            for participant_index in range(10):  # RLBot supports up to 10 participants
                team_key = f"participant_team_{participant_index}"
                name_key = f"participant_bot_name_{participant_index}"

                if team_key in parser["Match Setup"]:
                    # Try to get bot name
                    if name_key in parser["Match Setup"]:
                        name = parser["Match Setup"][name_key]
                    else:
                        # Fallback: try to read from participant config
                        name = f"Bot{participant_index}"

                    self.bot_name_map[participant_index] = name
                    self.logger.info(f"Mapped participant {participant_index} -> {name}")

        except Exception as e:
            self.logger.warning(f"Failed to load rlbot.cfg: {e}")

    def start(self) -> None:
        """Start the match monitor thread."""
        if self.monitor_thread is not None:
            self.logger.warning("Monitor thread already running")
            return

        self.should_stop = False
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()
        self.logger.info("Match monitor started")

    def stop(self) -> None:
        """Stop the match monitor thread."""
        self.should_stop = True
        if self.monitor_thread:
            self.monitor_thread.join(timeout=5)
            self.monitor_thread = None
        self.logger.info("Match monitor stopped")

    def _monitor_loop(self) -> None:
        """Main monitoring loop (runs in background thread)."""
        self.logger.info("Match monitor loop started")

        while not self.should_stop:
            try:
                # Get current game state
                packet = game_interface.get_packet()

                # Check if match has ended
                if self._check_match_ended(packet):
                    self._handle_match_completion(packet)

                # Small sleep to avoid busy-waiting
                time.sleep(0.016)  # ~60 Hz

            except Exception as e:
                self.logger.error(f"Error in monitor loop: {e}")
                time.sleep(0.1)

    def _check_match_ended(self, packet) -> bool:
        """
        Check if a match has just ended.
        Returns True only on the frame when match ends (not continuously).
        """
        try:
            # Check if game is in active state
            game_info = packet.game_info

            # RLBot GameTickPacket has various states we can check
            # Look for match_ended flag or game state transition
            is_match_ended = getattr(game_info, 'is_match_ended', False)

            # Detect transition from in-progress to ended
            if is_match_ended and not self.last_match_ended:
                self.last_match_ended = True
                return True
            elif not is_match_ended:
                self.last_match_ended = False

            return False

        except Exception as e:
            self.logger.debug(f"Error checking match state: {e}")
            return False

    def _handle_match_completion(self, packet) -> None:
        """Handle a completed match by extracting and logging the result."""
        try:
            gamemode = self._detect_gamemode(packet)
            team1_bots, team1_goals = self._extract_team_info(packet, team=0)
            team2_bots, team2_goals = self._extract_team_info(packet, team=1)

            self.logger.info(
                f"Match completed: {gamemode} - "
                f"Team1({team1_goals}): {team1_bots} vs "
                f"Team2({team2_goals}): {team2_bots}"
            )

            # Skip if it's a draw
            if team1_goals == team2_goals:
                self.logger.info("Match was a draw, skipping logging")
                return

            # Ensure team1 is the winner
            if team1_goals < team2_goals:
                team1_bots, team2_bots = team2_bots, team1_bots
                team1_goals, team2_goals = team2_goals, team1_goals

            # Log to tracker
            success = self.tracker.write_match(
                gamemode=gamemode,
                team1_bots=team1_bots,
                team2_bots=team2_bots,
                team1_goals=team1_goals,
                team2_goals=team2_goals
            )

            if success:
                self.logger.info("Match successfully logged to tracker")
            else:
                self.logger.error("Failed to log match to tracker")

        except Exception as e:
            self.logger.error(f"Error handling match completion: {e}")

    def _detect_gamemode(self, packet) -> str:
        """Detect the gamemode from the number of players."""
        try:
            # Count active players
            players = packet.players
            team0_count = sum(1 for p in players if p.team == 0)
            team1_count = sum(1 for p in players if p.team == 1)

            total = team0_count + team1_count

            if total == 2:
                return "1v1"
            elif total == 4:
                return "2v2"  # Could be soloq2v2 too, but we'll detect from bots
            elif total == 6:
                return "3v3"  # Could be soloq3v3 too

            return "unknown"

        except Exception as e:
            self.logger.error(f"Error detecting gamemode: {e}")
            return "unknown"

    def _extract_team_info(self, packet, team: int) -> tuple:
        """
        Extract bot names and goals for a team.

        Args:
            packet: GameTickPacket
            team: Team number (0 or 1)

        Returns:
            Tuple of (bot_names_list, goals)
        """
        try:
            bots = []
            goals = 0

            players = packet.players
            for player in players:
                if player.team != team:
                    continue

                # Get bot name from index mapping or use player name
                if player.index in self.bot_name_map:
                    name = self.bot_name_map[player.index]
                else:
                    name = getattr(player, 'name', f'Bot{player.index}')

                bots.append(name)

            # Get team score
            teams = packet.teams
            if team < len(teams):
                goals = teams[team].score

            return bots, goals

        except Exception as e:
            self.logger.error(f"Error extracting team info: {e}")
            return [], 0

    def get_current_mmr(self, bot_name: str, gamemode: str) -> float:
        """Get current MMR for a bot."""
        return self.tracker.get_bot_mmr(bot_name, gamemode)

    def get_all_bots(self) -> List[str]:
        """Get list of all bots that have played."""
        return self.tracker.get_all_bots()

    def get_gamemode_mmrs(self, gamemode: str) -> Dict[str, float]:
        """Get MMRs for all bots in a gamemode."""
        return self.tracker.get_gamemode_mmrs(gamemode)
