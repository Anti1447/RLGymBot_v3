#include "AdvancedObsPadded.h"
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <algorithm>
#include <numeric>

void RLGC::AdvancedObsPadded::Reset(const GameState& initialState) {
	// Initialize demo timers for all players (0 = not demoed)
	demoTimers.clear();
	demoTimers.resize(initialState.players.size(), 0.0f);
}

void RLGC::AdvancedObsPadded::AddPlayerToObsWithDemoTimer(
	FList& obs,
	const Player& player,
	bool inv,
	const PhysState& ball,
	float demoTimer
) {
	// Base class AddPlayerToObs captures:
	// - Position (inverted if needed)
	// - Rotation matrix (forward, up vectors)
	// - Velocity (inverted if needed)
	// - Angular velocity (global and local)
	// - Local ball position (relative to car)
	// - Local ball velocity (relative to car)
	// - Boost level (normalized 0-100 to 0-1)
	// - On ground status
	// - Has flip/jump status
	// - Demoed status
	//
	// We then add:
	// - Demo timer (how long until respawn)

	AddPlayerToObs(obs, player, inv, ball);

	// Add demo timer (normalized to [0, 1])
	// Typical demo respawn time is ~3 seconds in real Rocket League
	// We normalize by dividing by a reasonable max (e.g., 3.0f)
	float normalizedDemoTimer = RS_CLAMP(demoTimer / 3.0f, 0.0f, 1.0f);
	obs += normalizedDemoTimer;
}

RLGC::FList RLGC::AdvancedObsPadded::BuildObs(const Player& player, const GameState& state) {
	FList obs = {};

	bool inv = player.team == Team::ORANGE;

	auto ball = InvertPhys(state.ball, inv);
	auto& pads = state.GetBoostPads(inv);
	auto& padTimers = state.GetBoostPadTimers(inv);

	// =========================================================================
	// BALL STATE (GLOBAL)
	// =========================================================================
	obs += ball.pos * POS_COEF;
	obs += ball.vel * VEL_COEF;
	obs += ball.angVel * ANG_VEL_COEF;

	// =========================================================================
	// PREVIOUS ACTION
	// =========================================================================
	for (int i = 0; i < player.prevAction.ELEM_AMOUNT; i++)
		obs += player.prevAction[i];

	// =========================================================================
	// BOOST PAD STATE (WITH TIMERS)
	// =========================================================================
	// Clever blend: if pad is available, output 1.0
	// If not available, output a value that increases as the pad respawns
	// This helps the network learn when pads will become available
	// Inspired by Nexto's approach
	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		if (pads[i]) {
			obs += 1.f;  // Pad is available
		}
		else {
			obs += 1.f / (1.f + padTimers[i]);  // Respawn progress
		}
	}

	// =========================================================================
	// SELF PLAYER OBSERVATION (GLOBAL + LOCAL PERSPECTIVE + DEMO TIMER)
	// =========================================================================
	AddPlayerToObsWithDemoTimer(obs, player, inv, ball, 0.0f);  // Self is never demoed
	int playerObsSize = 0;
	{
		// Calculate player obs size by building a dummy observation
		FList dummy = {};
		AddPlayerToObsWithDemoTimer(dummy, player, inv, ball, 0.0f);
		playerObsSize = dummy.size();
	}

	// =========================================================================
	// DEMO TIMER TRACKING
	// =========================================================================
	// Update demo timers based on player state
	// Demo timer counts down from ~3 seconds to 0
	if (demoTimers.size() != state.players.size()) {
		demoTimers.resize(state.players.size(), 0.0f);
	}

	for (size_t i = 0; i < state.players.size(); i++) {
		const Player& p = state.players[i];

		if (p.isDemoed) {
			// Player is currently demoed, keep timer running
			if (demoTimers[i] < 3.0f) {
				demoTimers[i] += CommonValues::TICK_TIME;
			}
		}
		else {
			// Player is not demoed, reset timer
			demoTimers[i] = 0.0f;
		}
	}

	// =========================================================================
	// TEAMMATES AND OPPONENTS (DYNAMIC WITH PADDING + DEMO TIMERS)
	// =========================================================================
	std::vector<FList> teammates = {}, opponents = {};
	std::vector<float> teammateDemoTimers = {}, opponentDemoTimers = {};

	for (size_t i = 0; i < state.players.size(); i++) {
		const Player& otherPlayer = state.players[i];

		if (otherPlayer.carId == player.carId)
			continue;

		FList playerObs = {};
		float demoTimer = demoTimers[i];
		AddPlayerToObsWithDemoTimer(playerObs, otherPlayer, inv, ball, demoTimer);

		if (otherPlayer.team == player.team) {
			teammates.push_back(playerObs);
			teammateDemoTimers.push_back(demoTimer);
		}
		else {
			opponents.push_back(playerObs);
			opponentDemoTimers.push_back(demoTimer);
		}
	}

	if (teammates.size() > (size_t)(maxPlayers - 1))
		RG_ERR_CLOSE("AdvancedObsPadded: Too many teammates for Obs, maximum is " << (maxPlayers - 1));

	if (opponents.size() > (size_t)maxPlayers)
		RG_ERR_CLOSE("AdvancedObsPadded: Too many opponents for Obs, maximum is " << maxPlayers);

	// Pad to target sizes
	for (int i = 0; i < 2; i++) {
		auto& playerList = i ? teammates : opponents;
		int targetCount = i ? maxPlayers - 1 : maxPlayers;

		while ((int)playerList.size() < targetCount) {
			FList pad = FList(playerObsSize);
			playerList.push_back(pad);
		}
	}

	// Shuffle both lists AND their associated demo timers together
	// This prevents slot bias while keeping demo timers aligned
	std::vector<size_t> tmateIndices(teammates.size());
	std::iota(tmateIndices.begin(), tmateIndices.end(), 0);
	std::shuffle(tmateIndices.begin(), tmateIndices.end(), ::Math::GetRandEngine());

	std::vector<size_t> oppIndices(opponents.size());
	std::iota(oppIndices.begin(), oppIndices.end(), 0);
	std::shuffle(oppIndices.begin(), oppIndices.end(), ::Math::GetRandEngine());

	// Append shuffled teammates
	for (size_t idx : tmateIndices) {
		obs += teammates[idx];
	}

	// Append shuffled opponents
	for (size_t idx : oppIndices) {
		obs += opponents[idx];
	}

	return obs;
}
