#pragma once
#include "../Gamestates/GameState.h"
#include "../BasicTypes/Action.h"
#include "../TerminalConditions/TerminalCondition.h"
#include "../Rewards/Reward.h"
#include "../OBSBuilders/OBSBuilder.h"
#include "../ActionParsers/ActionParser.h"
#include "../StateSetters/StateSetter.h"
#include "../ThreadPool.h"
#include "../Utils/KickoffController.h"
#include <RLGymCPP/Rewards/Reward.h>
#include <map>

namespace RLGC {

	struct EnvCreateResult {
		Arena* arena;
		std::vector<WeightedReward> rewards;
		std::vector<TerminalCondition*> terminalConditions;
		ObsBuilder* obsBuilder;
		ActionParser* actionParser;
		StateSetter* stateSetter;

		void* userInfo; // Optional userinfo pointer if you want to track some data per-env
	};
	typedef std::function<EnvCreateResult(int index)> EnvCreateFn;

	struct EnvSetConfig {
		EnvCreateFn envCreateFn;
		int numArenas;
		int tickSkip;
		int actionDelay;
		bool saveRewards;
		bool shuffleRewardSampling = true;
		KickoffMode kickoffMode = KickoffMode::HARDCODED;  // ADD THIS
		bool enableProfiling = false;
	};

	struct EnvState {
		int numPlayers;
		std::vector<GameState> gameStates;
		std::vector<GameState> prevGameStates;
		DimList2<float> obs;
		DimList2<uint8_t> actionMasks;
		std::vector<float> rewards;
		std::vector<std::vector<float>> lastRewards; // Only from the first arena
		std::vector<uint8_t> terminals;

		std::vector<int> arenaPlayerStartIdx = {};

		void Resize(std::vector<Arena*>& arenas) {
			numPlayers = 0;
			for (int i = 0; i < arenas.size(); i++) {
				arenaPlayerStartIdx.push_back(numPlayers);
				numPlayers += arenas[i]->_cars.size();
			}

			gameStates.resize(arenas.size());
			prevGameStates.resize(arenas.size());
			rewards.resize(numPlayers);
			lastRewards.resize(arenas.size());
			terminals.resize(arenas.size());
		}
	};

	struct EnvSet {

		struct CallbackUserInfo {
			RLGC::EnvSet* envSet;
			Arena* arena;
			int arenaIdx;
		};

		std::vector<Arena*> arenas;
		std::vector<GameEventTracker*> eventTrackers;

		std::vector<CallbackUserInfo*> eventCallbackInfos;
		std::vector<void*> userInfos;

		EnvSetConfig config;

		int obsSize;
		int numActions;

		std::vector<std::vector<WeightedReward>> rewards;
		std::vector<std::vector<TerminalCondition*>> terminalConditions;
		std::vector<ObsBuilder*> obsBuilders;
		std::vector<KickoffController> kickoff_controllers;
		std::vector<std::vector<int>> kickoff_indices;
		std::vector<ActionParser*> actionParsers;
		std::vector<StateSetter*> stateSetters;

		EnvState state = {};

	// Profiling data
	struct ProfilingData {
		std::vector<double> actionParsingTimes;
		std::vector<double> physicsSimTimes;
		std::vector<double> rewardCompTimes;
		std::vector<double> obsBuilderTimes;
		std::vector<double> actionMaskTimes;
		std::vector<double> totalStepTimes;
		std::vector<int> stepCounts;  // Number of steps accumulated
		std::map<std::string, double> perRewardTimes;  // Individual reward function times

		ProfilingData(int numArenas) {
			actionParsingTimes.resize(numArenas, 0.0);
			physicsSimTimes.resize(numArenas, 0.0);
			rewardCompTimes.resize(numArenas, 0.0);
			obsBuilderTimes.resize(numArenas, 0.0);
			actionMaskTimes.resize(numArenas, 0.0);
			totalStepTimes.resize(numArenas, 0.0);
			stepCounts.resize(numArenas, 0);
		}
	} profiling;

	// Pre-allocated temporary vectors for reward computation (one per arena)
	std::vector<FList> tempRewardVectors;

		EnvSet(const EnvSetConfig& config);

		RG_NO_COPY(EnvSet);

		~EnvSet() {
			for (Arena* arena : arenas)
				delete arena;

			for (auto& eventTracker : eventTrackers)
				delete eventTracker;
			for (auto& eventCallbackInfo : eventCallbackInfos)
				delete eventCallbackInfo;
		}

		////////////////////
		
		void StepFirstHalf(bool async);
		void StepSecondHalf(const IList& actionIndices, bool async);
		void Sync() { g_ThreadPool.WaitUntilDone(); }
		void ResetArena(int index);
		void Reset();

	// Profiling support
	void PrintProfilingData() const;
	void ResetProfilingData();
	};
}