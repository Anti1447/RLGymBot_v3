#pragma once
#include "TerminalCondition.h"
#include <map>

namespace RLGC {
	class DemoCountCondition : public TerminalCondition {
	private:
		int maxDemos;
		mutable std::map<uint32_t, int> demoCounts;  // Per-player demo accumulation

	public:
		// Constructor: specify how many total demos end the episode
		DemoCountCondition(int maxDemos) : maxDemos(maxDemos) {
		}

		virtual void Reset(const GameState& initialState) override {
			demoCounts.clear();
		}

		virtual bool IsTerminal(const GameState& currentState) override {
			// Iterate all players, accumulate demo counts
			for (const auto& player : currentState.players) {
				if (player.eventState.demo) {
					demoCounts[player.carId]++;
				}
			}

			// Sum all demos from all players
			int totalDemos = 0;
			for (const auto& pair : demoCounts) {
				totalDemos += pair.second;
			}

			return totalDemos >= maxDemos;
		}

		virtual bool IsTruncation() override {
			return true;  // Demo count is a training constraint, not a game rule
		}
	};
}
