#include "EnvSet.h"
#include  "../Rewards/ZeroSumReward.h"
#include <chrono>
#include <iomanip>

template<bool RLGC::PlayerEventState::* DATA_VAR>
void IncPlayerCounter(Car* car, void* userInfoPtr) {
	if (!car)
		return;

	auto userInfo = (RLGC::EnvSet::CallbackUserInfo*)userInfoPtr;

	auto& gs = userInfo->envSet->state.gameStates[userInfo->arenaIdx];
	for (auto& player : gs.players)
		if (player.carId == car->id)
			(player.eventState.*DATA_VAR) = true;
}

void _ShotEventCallback(Arena* arena, Car* shooter, Car* passer, void* userInfo) {
	IncPlayerCounter<&RLGC::PlayerEventState::shot>(shooter, userInfo);
	IncPlayerCounter<&RLGC::PlayerEventState::shotPass>(passer, userInfo);
}

void _GoalEventCallback(Arena* arena, Car* scorer, Car* passer, void* userInfo) {
	IncPlayerCounter<&RLGC::PlayerEventState::goal>(scorer, userInfo);
	IncPlayerCounter<&RLGC::PlayerEventState::assist>(passer, userInfo);
}

void _SaveEventCallback(Arena* arena, Car* saver, void* userInfo) {
	IncPlayerCounter<&RLGC::PlayerEventState::save>(saver, userInfo);
}

void _BumpCallback(Arena* arena, Car* bumper, Car* victim, bool isDemo, void* userInfo) {
	if (bumper->team == victim->team)
		return;

	IncPlayerCounter<&RLGC::PlayerEventState::bump>(bumper, userInfo);
	IncPlayerCounter<&RLGC::PlayerEventState::bumped>(victim, userInfo);

	if (isDemo) {
		IncPlayerCounter<&RLGC::PlayerEventState::demo>(bumper, userInfo);
		IncPlayerCounter<&RLGC::PlayerEventState::demoed>(victim, userInfo);
	}
}

/////////////////////////////

RLGC::EnvSet::EnvSet(const EnvSetConfig& config) : config(config), profiling(config.numArenas) {

	RG_ASSERT(config.tickSkip > 0);
	RG_ASSERT(config.actionDelay >= 0 && config.actionDelay <= config.tickSkip);

	std::mutex appendMutex = {};
	auto fnCreateArenas = [&](int idx) {
		auto createResult = config.envCreateFn(idx);
		auto arena = createResult.arena;

		appendMutex.lock();
		{
			arenas.push_back(arena);

			auto userInfo = new CallbackUserInfo();
			userInfo->arena = arena;
			userInfo->arenaIdx = idx;
			userInfo->envSet = this;
			eventCallbackInfos.push_back(userInfo);
			arena->SetCarBumpCallback(_BumpCallback, userInfo);

			if (arena->gameMode != GameMode::HEATSEEKER) {
				GameEventTracker* tracker = new GameEventTracker({});
				eventTrackers.push_back(tracker);

				tracker->SetShotCallback(_ShotEventCallback, userInfo);
				tracker->SetGoalCallback(_GoalEventCallback, userInfo);
				tracker->SetSaveCallback(_SaveEventCallback, userInfo);
			} else {
				eventTrackers.push_back(NULL);
				eventCallbackInfos.push_back(NULL);
			}

			userInfos.push_back(createResult.userInfo);

			rewards.push_back(createResult.rewards);
			terminalConditions.push_back(createResult.terminalConditions);
			obsBuilders.push_back(createResult.obsBuilder);
			actionParsers.push_back(createResult.actionParser);
			stateSetters.push_back(createResult.stateSetter);
		}
		appendMutex.unlock();
	};
	g_ThreadPool.StartBatchedJobs(fnCreateArenas, config.numArenas, false);

	state.Resize(arenas);
	
	// Determine obs size and action amount, initialize arrays accordingly
	{
		stateSetters[0]->ResetArena(arenas[0]);
		GameState testState = GameState(arenas[0]);
		testState.userInfo = userInfos[0];
		obsBuilders[0]->Reset(testState);
		obsSize = obsBuilders[0]->BuildObs(testState.players[0], testState).size();
		state.obs = DimList2<float>(state.numPlayers, obsSize);

		state.actionMasks = DimList2<uint8_t>(state.numPlayers, actionParsers[0]->GetActionAmount());
	}

	// Reset all arenas initially
	g_ThreadPool.StartBatchedJobs(
		std::bind(&RLGC::EnvSet::ResetArena, this, std::placeholders::_1),
		config.numArenas, false
	);

	// After initializing all arenas, add this:
	for (int i = 0; i < arenas.size(); i++) {
		kickoff_controllers.push_back(KickoffController(config.kickoffMode));  // Pass mode
		kickoff_indices.push_back(std::vector<int>(arenas[i]->_cars.size(), -1));
	}

	// Initialize temporary vectors for reward computation
	for (int i = 0; i < config.numArenas; i++) {
		int numPlayers = arenas[i]->_cars.size();
		tempRewardVectors.push_back(FList(numPlayers, 0.0f));
	}
	
}

void RLGC::EnvSet::StepFirstHalf(bool async) {

	auto fnStepArena = [&](int arenaIdx) {
		Arena* arena = arenas[arenaIdx];
		auto& gs = state.gameStates[arenaIdx];

		{
			// Set previous gamestates
			state.prevGameStates[arenaIdx] = gs;
		}

		gs.ResetBeforeStep();

		// Step arena with old actions
		arena->Step(config.actionDelay);
	};

	g_ThreadPool.StartBatchedJobs(fnStepArena, arenas.size(), async);
}

void RLGC::EnvSet::StepSecondHalf(const IList& actionIndices, bool async) {

	auto fnStepArenas = [&](int arenaIdx) {
		auto totalStepStart = std::chrono::high_resolution_clock::now();

		Arena* arena = arenas[arenaIdx];
		auto& gs = state.gameStates[arenaIdx];
		int playerStartIdx = state.arenaPlayerStartIdx[arenaIdx];
		int numPlayers = gs.players.size();

		// Parse and set actions
		auto start = std::chrono::high_resolution_clock::now();
		auto actions = std::vector<Action>(numPlayers);
		auto carItr = arena->_cars.begin();
		for (int i = 0; i < numPlayers; i++, carItr++) {
			auto& player = gs.players[i];
			auto& kickoff = kickoff_controllers[arenaIdx];
			auto& kickoff_idx = kickoff_indices[arenaIdx];
			Car* car = *carItr;
			// ===== KICKOFF HANDLING =====
			Action action;

			if (kickoff.GetMode() == KickoffMode::HARDCODED && kickoff.should_begin_kickoff(gs)) {
				// Initialize kickoff ONLY on first frame
				if (kickoff_idx[i] == -1) {
					kickoff_idx[i] = kickoff.is_player_kickoff_taker(player, gs) ? 0 : -2;
				}

				// Execute hardcoded kickoff if taker
				if (kickoff_idx[i] >= 0 && kickoff_idx[i] < kickoff.get_sequence_length()) {
					action = kickoff.get_kickoff_action(kickoff_idx[i], player.team);
					kickoff_idx[i]++;
				}
				else {
					// Not taker or sequence complete, use NN
					action = actionParsers[arenaIdx]->ParseAction(actionIndices[playerStartIdx + i], player, gs);
					if (kickoff_idx[i] >= kickoff.get_sequence_length()) {
						kickoff_idx[i] = -1;
					}
				}
			}
			else {
				// Stochastic mode or not in kickoff - always use NN
				action = actionParsers[arenaIdx]->ParseAction(actionIndices[playerStartIdx + i], player, gs);
				kickoff_idx[i] = -1;
			}
			car->controls = (CarControls)action;
			actions[i] = action;
		}
		if (config.enableProfiling) {
			auto elapsed = std::chrono::high_resolution_clock::now() - start;
			profiling.actionParsingTimes[arenaIdx] += std::chrono::duration<double, std::milli>(elapsed).count();
		}

		// Step arena with new actions we got from observing the last state
		// Update the gamestate after
		start = std::chrono::high_resolution_clock::now();
		{
			arena->Step(config.tickSkip - config.actionDelay);

			if (eventTrackers[arenaIdx])
				eventTrackers[arenaIdx]->Update(arena);

			GameState* gsPrev = &state.prevGameStates[arenaIdx];
			if (gsPrev->IsEmpty())
				gsPrev = NULL;

			gs.UpdateFromArena(arena, actions, gsPrev);
		}
		if (config.enableProfiling) {
			auto elapsed = std::chrono::high_resolution_clock::now() - start;
			profiling.physicsSimTimes[arenaIdx] += std::chrono::duration<double, std::milli>(elapsed).count();
		}

		// Update terminal
		uint8_t terminalType = TerminalType::NOT_TERMINAL;
		{
			for (auto cond : terminalConditions[arenaIdx]) {
				if (cond->IsTerminal(gs)) {
					bool isTrunc = cond->IsTruncation();
					uint8_t curTerminalType = isTrunc ? TerminalType::TRUNCATED : TerminalType::NORMAL;
					if (terminalType == TerminalType::NOT_TERMINAL) {
						terminalType = curTerminalType;
					} else {
						// We already know this state is terminal
						// However, if we only know it is a truncated terminal, we should let normal terminals take priority
						// (Normal terminals are better information than truncations)
						if (curTerminalType == TerminalType::NORMAL)
							terminalType = curTerminalType;
					}

					// NOTE: We can't break since terminal conditions are guaranteed to be called once per step
				}
			}
			state.terminals[arenaIdx] = terminalType;
		}

		// Pre-step rewards
		{
			for (auto& weighted : rewards[arenaIdx])
				weighted.reward->PreStep(gs);
		}

		// Update rewards - use pre-allocated vector and reuse
		start = std::chrono::high_resolution_clock::now();
		{
			auto& allRewards = tempRewardVectors[arenaIdx];
			std::fill(allRewards.begin(), allRewards.end(), 0.0f);

			for (size_t rewardIdx = 0; rewardIdx < rewards[arenaIdx].size(); rewardIdx++) {
				auto& weightedReward = rewards[arenaIdx][rewardIdx];

				auto rewardStart = config.enableProfiling ? std::chrono::high_resolution_clock::now() : std::chrono::high_resolution_clock::time_point();
				FList output = weightedReward.reward->GetAllRewards(gs, terminalType);
				if (config.enableProfiling) {
					auto elapsed = std::chrono::high_resolution_clock::now() - rewardStart;
					std::string rewardName = weightedReward.reward->GetName();
					profiling.perRewardTimes[rewardName] += std::chrono::duration<double, std::milli>(elapsed).count();
				}

				for (int i = 0; i < numPlayers; i++)
					allRewards[i] += output[i] * weightedReward.weight;

				// Save the reward
				if (config.saveRewards) {
					int playerSampleIndex;
					if (config.shuffleRewardSampling) {
						playerSampleIndex = Math::RandInt(0, output.size());
					} else {
						// Find player with the lowest id
						playerSampleIndex = 0;
						uint32_t lowestID = gs.players[0].carId;
						for (int i = 1; i < numPlayers; i++) {
							auto id = gs.players[i].carId;
							if (id < lowestID) {
								lowestID = id;
								playerSampleIndex = i;
							}
						}
					}
					// We will only take the reward from a random player
					float rewardToSave = output[playerSampleIndex];

					// If zero-sum, use the inner reward
					if (ZeroSumReward* zeroSum = dynamic_cast<ZeroSumReward*>(weightedReward.reward))
						rewardToSave = zeroSum->_lastRewards[playerSampleIndex];

					// If needed, initialize last rewards
					if (state.lastRewards[arenaIdx].empty())
						state.lastRewards[arenaIdx].resize(rewards[arenaIdx].size());

					state.lastRewards[arenaIdx][rewardIdx] = rewardToSave;
				}
			}

			for (int i = 0; i < numPlayers; i++)
				state.rewards[playerStartIdx + i] = allRewards[i];
		}
		if (config.enableProfiling) {
			auto elapsed = std::chrono::high_resolution_clock::now() - start;
			profiling.rewardCompTimes[arenaIdx] += std::chrono::duration<double, std::milli>(elapsed).count();
		}

		// Update observations
		start = std::chrono::high_resolution_clock::now();
		{
			for (int i = 0; i < numPlayers; i++)
				state.obs.Set(playerStartIdx + i, obsBuilders[arenaIdx]->BuildObs(gs.players[i], gs));
		}
		if (config.enableProfiling) {
			auto elapsed = std::chrono::high_resolution_clock::now() - start;
			profiling.obsBuilderTimes[arenaIdx] += std::chrono::duration<double, std::milli>(elapsed).count();
		}

		// Update action masks
		start = std::chrono::high_resolution_clock::now();
		{
			for (int i = 0; i < numPlayers; i++)
				state.actionMasks.Set(playerStartIdx + i, actionParsers[arenaIdx]->GetActionMask(gs.players[i], gs));
		}
		if (config.enableProfiling) {
			auto elapsed = std::chrono::high_resolution_clock::now() - start;
			profiling.actionMaskTimes[arenaIdx] += std::chrono::duration<double, std::milli>(elapsed).count();
		}

		if (config.enableProfiling) {
			auto totalElapsed = std::chrono::high_resolution_clock::now() - totalStepStart;
			profiling.totalStepTimes[arenaIdx] += std::chrono::duration<double, std::milli>(totalElapsed).count();
			profiling.stepCounts[arenaIdx]++;
		}
	};

	g_ThreadPool.StartBatchedJobs(fnStepArenas, arenas.size(), async);
}

void RLGC::EnvSet::ResetArena(int index) {
	stateSetters[index]->ResetArena(arenas[index]);
	GameState newState = GameState(arenas[index]);
	state.gameStates[index] = newState;

	newState.userInfo = userInfos[index];

	// Update event tracker
	if (eventTrackers[index])
		eventTrackers[index]->ResetPersistentInfo();

	// Reset all the other stuff
	obsBuilders[index]->Reset(newState);
	for (auto& cond : terminalConditions[index])
		cond->Reset(newState);
	for (auto& weightedReward : rewards[index])
		weightedReward.reward->Reset(newState);

	int playerStartIdx = state.arenaPlayerStartIdx[index];
	for (int i = 0; i < newState.players.size(); i++) {

		// Update obs
		auto obs = obsBuilders[index]->BuildObs(newState.players[i], newState);
		state.obs.Set(playerStartIdx + i, obs);

		// Update action mask
		auto actionMask = actionParsers[index]->GetActionMask(newState.players[i], newState);
		state.actionMasks.Set(playerStartIdx + i, actionMask);
	}

	// Remove previous state
	state.prevGameStates[index].MakeEmpty();
}

void RLGC::EnvSet::PrintProfilingData() const {
	if (!config.enableProfiling) {
		// Log a message that profiling is disabled
		return;
	}

	// Calculate statistics
	double avgActionParsing = 0, avgPhysicsSim = 0, avgRewardComp = 0;
	double avgObsBuilder = 0, avgActionMask = 0, avgTotal = 0;
	int totalSteps = 0;

	for (int i = 0; i < arenas.size(); i++) {
		if (profiling.stepCounts[i] > 0) {
			avgActionParsing += profiling.actionParsingTimes[i] / profiling.stepCounts[i];
			avgPhysicsSim += profiling.physicsSimTimes[i] / profiling.stepCounts[i];
			avgRewardComp += profiling.rewardCompTimes[i] / profiling.stepCounts[i];
			avgObsBuilder += profiling.obsBuilderTimes[i] / profiling.stepCounts[i];
			avgActionMask += profiling.actionMaskTimes[i] / profiling.stepCounts[i];
			avgTotal += profiling.totalStepTimes[i] / profiling.stepCounts[i];
			totalSteps++;
		}
	}

	int numArenas = arenas.size();
	if (totalSteps > 0) {
		avgActionParsing /= totalSteps;
		avgPhysicsSim /= totalSteps;
		avgRewardComp /= totalSteps;
		avgObsBuilder /= totalSteps;
		avgActionMask /= totalSteps;
		avgTotal /= totalSteps;
	}

	char buffer[1024];
	snprintf(buffer, sizeof(buffer),
		"\n[EnvSet Profiling] %d arenas\n"
		"  Action Parsing:    %.3f ms (%.1f%%)\n"
		"  Physics Sim:       %.3f ms (%.1f%%)\n"
		"  Reward Comp:       %.3f ms (%.1f%%)\n"
		"  Obs Builder:       %.3f ms (%.1f%%)\n"
		"  Action Mask:       %.3f ms (%.1f%%)\n"
		"  Total Step Time:   %.3f ms\n",
		numArenas,
		avgActionParsing, (avgActionParsing / avgTotal) * 100.0,
		avgPhysicsSim, (avgPhysicsSim / avgTotal) * 100.0,
		avgRewardComp, (avgRewardComp / avgTotal) * 100.0,
		avgObsBuilder, (avgObsBuilder / avgTotal) * 100.0,
		avgActionMask, (avgActionMask / avgTotal) * 100.0,
		avgTotal);
	RG_LOG(buffer);

	// Print per-reward timing if available
	if (!profiling.perRewardTimes.empty()) {
		RG_LOG("  Per-Reward Breakdown:");
		for (const auto& [name, time] : profiling.perRewardTimes) {
			double percent = (time / (avgRewardComp * arenas.size() * (profiling.stepCounts[0] > 0 ? profiling.stepCounts[0] : 1))) * 100.0;
			RG_LOG("    " << name << ": " << std::fixed << std::setprecision(3) << time << " ms");
		}
	}

	std::cout << std::flush;
}

void RLGC::EnvSet::ResetProfilingData() {
	std::fill(profiling.actionParsingTimes.begin(), profiling.actionParsingTimes.end(), 0.0);
	std::fill(profiling.physicsSimTimes.begin(), profiling.physicsSimTimes.end(), 0.0);
	std::fill(profiling.rewardCompTimes.begin(), profiling.rewardCompTimes.end(), 0.0);
	std::fill(profiling.obsBuilderTimes.begin(), profiling.obsBuilderTimes.end(), 0.0);
	std::fill(profiling.actionMaskTimes.begin(), profiling.actionMaskTimes.end(), 0.0);
	std::fill(profiling.totalStepTimes.begin(), profiling.totalStepTimes.end(), 0.0);
	std::fill(profiling.stepCounts.begin(), profiling.stepCounts.end(), 0);
	profiling.perRewardTimes.clear();
}

void RLGC::EnvSet::Reset() {
	for (int i = 0; i < arenas.size(); i++)
		if (state.terminals[i])
			g_ThreadPool.StartJobAsync(std::bind(&EnvSet::ResetArena, this, std::placeholders::_1), i);
	std::fill(state.terminals.begin(), state.terminals.end(), 0);
	g_ThreadPool.WaitUntilDone();
}