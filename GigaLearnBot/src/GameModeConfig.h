#pragma once

#include <cstdlib>  // For rand() and RAND_MAX

namespace GGL {
	enum class GameModeType {
		MODE_1V1,
		MODE_2V2,
		MODE_3V3
	};

	struct GameModeConfig {
		// Distribution weights: 1v1:2v2:3v3 = 1:2:3
		// Total weight = 6
		static constexpr float WEIGHT_1V1 = 1.0f;
		static constexpr float WEIGHT_2V2 = 3.0f;
		static constexpr float WEIGHT_3V3 = 5.0f;
		static constexpr float TOTAL_WEIGHT = WEIGHT_1V1 + WEIGHT_2V2 + WEIGHT_3V3;  // 6.0

		// Cycling mode state (for render mode: cycles through 1v1 -> 2v2 -> 3v3)
		static int cycleIndex;

		// Get probability for each mode (normalized to [0,1])
		static float GetProbability(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_1V1: return WEIGHT_1V1 / TOTAL_WEIGHT;  // 1/6 ~= 0.167
				case GameModeType::MODE_2V2: return WEIGHT_2V2 / TOTAL_WEIGHT;  // 2/6 ~= 0.333
				case GameModeType::MODE_3V3: return WEIGHT_3V3 / TOTAL_WEIGHT;  // 3/6 ~= 0.500
				default: return 0.0f;
			}
		}

		// Sample a random mode based on distribution
		static GameModeType SampleMode() {
			float rand_val = (float)rand() / RAND_MAX;  // [0, 1)

			if (rand_val < GetProbability(GameModeType::MODE_1V1)) {
				return GameModeType::MODE_1V1;
			}
			else if (rand_val < GetProbability(GameModeType::MODE_1V1) + GetProbability(GameModeType::MODE_2V2)) {
				return GameModeType::MODE_2V2;
			}
			else {
				return GameModeType::MODE_3V3;
			}
		}

		// Cycle through modes: 1v1 -> 2v2 -> 3v3 -> 1v1 -> ...
		// Used for render mode to guarantee seeing all modes
		static GameModeType CycleMode() {
			GameModeType modes[] = { GameModeType::MODE_1V1, GameModeType::MODE_2V2, GameModeType::MODE_3V3 };
			GameModeType result = modes[cycleIndex % 3];
			cycleIndex++;
			return result;
		}

		// Get number of players per team for a given mode
		static int GetPlayersPerTeam(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_1V1: return 1;
				case GameModeType::MODE_2V2: return 2;
				case GameModeType::MODE_3V3: return 3;
				default: return 1;
			}
		}

		// Get mode name for logging
		static const char* GetModeName(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_1V1: return "1v1";
				case GameModeType::MODE_2V2: return "2v2";
				case GameModeType::MODE_3V3: return "3v3";
				default: return "UNKNOWN";
			}
		}
	};
}
