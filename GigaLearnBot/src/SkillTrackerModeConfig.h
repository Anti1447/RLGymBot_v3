#pragma once

#include <GigaLearnCPP/SkillTrackerConfig.h>
#include "GameModeConfig.h"

namespace GGL {
	struct SkillTrackerModeConfig {
		// Skill tracker configuration for each game mode
		SkillTrackerConfig config1v1;
		SkillTrackerConfig config2v2;
		SkillTrackerConfig config3v3;

		SkillTrackerModeConfig() {
			// Configure 1v1 skill tracker
			config1v1.enabled = true;
			config1v1.numArenas = 4;          // Fewer arenas needed for simpler mode
			config1v1.simTime = 45.f;
			config1v1.maxSimTime = 240.f;
			config1v1.updateInterval = 16;
			config1v1.ratingInc = 5.f;
			config1v1.initialRating = 0.f;
			config1v1.deterministic = false;

			// Configure 2v2 skill tracker
			config2v2.enabled = true;
			config2v2.numArenas = 8;          // Medium complexity
			config2v2.simTime = 45.f;
			config2v2.maxSimTime = 240.f;
			config2v2.updateInterval = 16;
			config2v2.ratingInc = 5.f;
			config2v2.initialRating = 0.f;
			config2v2.deterministic = false;

			// Configure 3v3 skill tracker
			config3v3.enabled = true;
			config3v3.numArenas = 16;         // Most arenas for most complex mode
			config3v3.simTime = 45.f;
			config3v3.maxSimTime = 240.f;
			config3v3.updateInterval = 16;
			config3v3.ratingInc = 5.f;
			config3v3.initialRating = 0.f;
			config3v3.deterministic = false;
		}

		// Get skill tracker config for a given mode
		SkillTrackerConfig GetConfig(GameModeType mode) const {
			switch (mode) {
				case GameModeType::MODE_1V1: return config1v1;
				case GameModeType::MODE_2V2: return config2v2;
				case GameModeType::MODE_3V3: return config3v3;
				default: return config3v3;  // Default to 3v3
			}
		}
	};
}
