#pragma once

#include <cstdlib>  // For rand() and RAND_MAX

namespace GGL {
	enum class GameModeType {
		MODE_SOLO,  // 1 blue car, 0 orange — freeplay training
		MODE_1V1,
		MODE_2V2,
		MODE_3V3
	};

	struct GameModeConfig {
		static constexpr float WEIGHT_SOLO = 0.0f;
		static constexpr float WEIGHT_1V1  = 6.0f;
		static constexpr float WEIGHT_2V2  = 6.0f;
		static constexpr float WEIGHT_3V3  = 9.0f;
		static constexpr float TOTAL_WEIGHT = WEIGHT_SOLO + WEIGHT_1V1 + WEIGHT_2V2 + WEIGHT_3V3;
		// Cycling mode state (for render mode: cycles through 1v1 -> 2v2 -> 3v3)
		static int cycleIndex;

		// Get probability for each mode (normalized to [0,1])
		static float GetProbability(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_SOLO: return WEIGHT_SOLO / TOTAL_WEIGHT;
				case GameModeType::MODE_1V1:  return WEIGHT_1V1  / TOTAL_WEIGHT;
				case GameModeType::MODE_2V2:  return WEIGHT_2V2  / TOTAL_WEIGHT;
				case GameModeType::MODE_3V3:  return WEIGHT_3V3  / TOTAL_WEIGHT;
				default: return 0.0f;
			}
		}

		// Sample a random mode based on distribution
		static GameModeType SampleMode() {
			float rand_val = (float)rand() / RAND_MAX;  // [0, 1)

			float cumSolo = GetProbability(GameModeType::MODE_SOLO);
			float cum1v1  = cumSolo + GetProbability(GameModeType::MODE_1V1);
			float cum2v2  = cum1v1  + GetProbability(GameModeType::MODE_2V2);

			if (rand_val < cumSolo) return GameModeType::MODE_SOLO;
			if (rand_val < cum1v1)  return GameModeType::MODE_1V1;
			if (rand_val < cum2v2)  return GameModeType::MODE_2V2;
			return GameModeType::MODE_3V3;
		}

		// Cycle through competitive modes: 1v1 -> 2v2 -> 3v3 -> 1v1 -> ...
		// Solo is excluded from render cycling (no opponent = less interesting to watch)
		static GameModeType CycleMode() {
			GameModeType modes[] = { GameModeType::MODE_1V1, GameModeType::MODE_2V2, GameModeType::MODE_3V3 };
			GameModeType result = modes[cycleIndex % 3];
			cycleIndex++;
			return result;
		}

		// Get number of blue-team players for a given mode
		// Note: SOLO means 1 blue car, 0 orange — arena creation is special-cased in ExampleMain
		static int GetPlayersPerTeam(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_SOLO: return 1;
				case GameModeType::MODE_1V1:  return 1;
				case GameModeType::MODE_2V2:  return 2;
				case GameModeType::MODE_3V3:  return 3;
				default: return 1;
			}
		}

		// Get mode name for logging
		static const char* GetModeName(GameModeType mode) {
			switch (mode) {
				case GameModeType::MODE_SOLO: return "solo";
				case GameModeType::MODE_1V1:  return "1v1";
				case GameModeType::MODE_2V2:  return "2v2";
				case GameModeType::MODE_3V3:  return "3v3";
				default: return "UNKNOWN";
			}
		}
	};
}
