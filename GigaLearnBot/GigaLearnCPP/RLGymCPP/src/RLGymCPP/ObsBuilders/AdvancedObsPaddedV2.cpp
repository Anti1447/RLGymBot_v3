#include "AdvancedObsPaddedV2.h"
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <algorithm>
#include <numeric>

void RLGC::AdvancedObsPaddedV2::Reset(const GameState& initialState) {
	EnvClock::Get().Reset();
	_oppTendencyByPlayer.clear();
	_selfSummaryByPlayer.clear();
}

void RLGC::AdvancedObsPaddedV2::AddPlayerToObsV2(
	FList& obs,
	const Player& player,
	bool inv,
	const PhysState& ball
) {
	// Base 29 floats: pos(3), forward(3), up(3), vel(3), angVel(3),
	//   rotMat.Dot(angVel)(3), rotMat.Dot(localBallPos)(3), rotMat.Dot(localBallVel)(3),
	//   boost(1), isOnGround(1), hasFlipOrJump(1), isDemoed(1), hasJumped(1)
	// RotMat.Dot(Vec) returns Vec (3 floats), confirmed in MathTypes.h:243
	AddPlayerToObs(obs, player, inv, ball);

	// Demo timer: use the field directly rather than tracking manually
	obs += RS_CLAMP(player.demoRespawnTimer / 3.0f, 0.0f, 1.0f);

	// New fields
	obs += player.hasDoubleJumped;
	obs += player.isSupersonic;
	obs += RS_CLAMP(player.airTimeSinceJump / 1.25f, 0.0f, 1.0f);  // 1.25s = max dodge window
}

void RLGC::AdvancedObsPaddedV2::TrimHistoryScalar(
	std::deque<std::pair<float, float>>& q, float now, float window)
{
	while (!q.empty() && (now - q.front().first) > window)
		q.pop_front();
}

void RLGC::AdvancedObsPaddedV2::TrimHistoryVec(
	std::deque<std::pair<float, Vec>>& q, float now, float window)
{
	while (!q.empty() && (now - q.front().first) > window)
		q.pop_front();
}

void RLGC::AdvancedObsPaddedV2::UpdateMemoryTrackers(
	const Player& perspective, const GameState& state)
{
	EnvClock::Get().Update(state);
	float now = EnvClock::Get().elapsedTime;

	Team myTeam  = perspective.team;
	Team oppTeam = (myTeam == Team::BLUE) ? Team::ORANGE : Team::BLUE;
	bool inv = (perspective.team == Team::ORANGE);

	TeamTendency& opp  = _oppTendencyByPlayer[perspective.carId];
	SelfSummary&  self = _selfSummaryByPlayer[perspective.carId];

	Vec  ballPos = state.ball.pos;
	float ballZ   = ballPos.z;
	float ballSpd = state.ball.vel.Length();

	// ---- Block A: opponent tendency ----
	bool  anyOppCommitted = false;
	bool  anyOppAerial    = false;
	bool  anyOppPressure  = false;
	bool  anyChallenge    = false;
	float nearestOppY     = 0.0f;
	float nearestOppDist  = 1e30f;
	float boostSum        = 0.0f;
	float speedSum        = 0.0f;
	int   oppCount        = 0;

	for (const auto& p : state.players) {
		if (p.team != oppTeam) continue;

		float distToBall = (p.pos - ballPos).Length();
		// Perspective-corrected Y: positive = toward opponent goal, negative = toward own goal
		float perspY = inv ? -p.pos.y : p.pos.y;

		if (distToBall < COMMITMENT_RADIUS) anyOppCommitted = true;
		if (p.pos.z > AERIAL_Z_THRESHOLD)   anyOppAerial    = true;
		if (perspY < 0.0f)                  anyOppPressure  = true;  // opponent in my half
		if (p.ballTouchedStep)              anyChallenge    = true;

		if (distToBall < nearestOppDist) {
			nearestOppDist = distToBall;
			nearestOppY    = perspY;
		}

		boostSum += p.boost / 100.0f;
		speedSum += p.vel.Length() / CommonValues::SUPERSONIC_THRESHOLD;
		oppCount++;
	}

	float boostMean = (oppCount > 0) ? (boostSum / oppCount) : 0.5f;
	float speedMean = (oppCount > 0) ? RS_CLAMP(speedSum / oppCount, 0.0f, 1.0f) : 0.5f;
	float depthNorm = RS_CLAMP(nearestOppY / CommonValues::BACK_WALL_Y, -1.0f, 1.0f);

	opp.commitmentEMA    += ALPHA_FAST * ((anyOppCommitted ? 1.0f : 0.0f) - opp.commitmentEMA);
	opp.aerialEMA        += ALPHA_FAST * ((anyOppAerial    ? 1.0f : 0.0f) - opp.aerialEMA);
	opp.depthMeanEMA     += ALPHA_MED  * (depthNorm        - opp.depthMeanEMA);
	opp.boostMeanEMA     += ALPHA_MED  * (boostMean        - opp.boostMeanEMA);
	opp.speedMeanEMA     += ALPHA_MED  * (speedMean        - opp.speedMeanEMA);
	opp.pressureEMA      += ALPHA_MED  * ((anyOppPressure  ? 1.0f : 0.0f) - opp.pressureEMA);
	opp.challengeRateEMA += ALPHA_SLOW * ((anyChallenge    ? 1.0f : 0.0f) - opp.challengeRateEMA);
	opp.sampleCount++;

	// ---- Block B: self and team summary ----
	for (const auto& p : state.players) {
		if (!p.ballTouchedStep) continue;
		if (p.carId == perspective.carId)
			self.myLastTouchTime   = now;
		else if (p.team == myTeam)
			self.teamLastTouchTime = now;
	}

	// Possession EMA — updated on touch events only (holds value between touches)
	bool myTeamTouched  = false;
	bool oppTeamTouched = false;
	for (const auto& p : state.players) {
		if (!p.ballTouchedStep) continue;
		if (p.team == myTeam) myTeamTouched  = true;
		else                  oppTeamTouched  = true;
	}
	if (myTeamTouched || oppTeamTouched) {
		float possValue = myTeamTouched ? 1.0f : 0.0f;
		self.possessionEMA += ALPHA_MED * (possValue - self.possessionEMA);
	}

	// Team avg dist to ball
	float teamDistSum = 0.0f;
	int   teamCount   = 0;
	for (const auto& p : state.players) {
		if (p.team != myTeam) continue;
		teamDistSum += (p.pos - ballPos).Length();
		teamCount++;
	}
	float teamAvgDist  = (teamCount > 0) ? (teamDistSum / teamCount) : 0.0f;
	float teamDistNorm = RS_CLAMP(teamAvgDist / TEAM_DIST_NORM, 0.0f, 1.0f);
	self.teamAvgDistToBallEMA += ALPHA_B8 * (teamDistNorm - self.teamAvgDistToBallEMA);

	// Rolling histories
	self.boostHistory.push_back({    now, perspective.boost / 100.0f });
	self.positionHistory.push_back({ now, perspective.pos           });
	self.ballZHistory.push_back({    now, ballZ                     });
	self.ballSpeedHistory.push_back({now, ballSpd                   });

	TrimHistoryScalar(self.boostHistory,     now, BOOST_HISTORY_SEC);
	TrimHistoryVec   (self.positionHistory,  now, POSITION_HISTORY_SEC);
	TrimHistoryScalar(self.ballZHistory,     now, BALL_Z_HISTORY_SEC);
	TrimHistoryScalar(self.ballSpeedHistory, now, BALL_SPEED_HISTORY_SEC);
}

void RLGC::AdvancedObsPaddedV2::AddMemoryBlocksAToObs(FList& obs, const Player& perspective) {
	const TeamTendency& opp = _oppTendencyByPlayer.at(perspective.carId);
	float confidence = RS_CLAMP((float)opp.sampleCount / CONFIDENCE_SAMPLES, 0.0f, 1.0f);

	obs += opp.commitmentEMA;     // A1 [0,1]
	obs += opp.aerialEMA;         // A2 [0,1]
	obs += opp.depthMeanEMA;      // A3 [-1,1] perspective-corrected in UpdateMemoryTrackers
	obs += opp.boostMeanEMA;      // A4 [0,1]
	obs += opp.speedMeanEMA;      // A5 [0,1]
	obs += opp.pressureEMA;       // A6 [0,1]
	obs += opp.challengeRateEMA;  // A7 [0,~0.1]
	obs += confidence;            // A8 [0,1]
}

void RLGC::AdvancedObsPaddedV2::AddMemoryBlocksBToObs(FList& obs, const Player& perspective) {
	const SelfSummary& self = _selfSummaryByPlayer.at(perspective.carId);
	float now = EnvClock::Get().elapsedTime;

	// B1: secs since MY last touch, normalized [0,1] (1 = TOUCH_TIMEOUT_NORM+ secs ago)
	float secsSinceMyTouch = (self.myLastTouchTime < 0.0f) ? TOUCH_TIMEOUT_NORM
		: RS_MIN(now - self.myLastTouchTime, TOUCH_TIMEOUT_NORM);
	obs += secsSinceMyTouch / TOUCH_TIMEOUT_NORM;

	// B2: secs since own-team's last touch (includes self), normalized [0,1]
	float bestTeamTouch = RS_MAX(self.myLastTouchTime, self.teamLastTouchTime);
	float secsSinceTeamTouch = (bestTeamTouch < 0.0f) ? TOUCH_TIMEOUT_NORM
		: RS_MIN(now - bestTeamTouch, TOUCH_TIMEOUT_NORM);
	obs += secsSinceTeamTouch / TOUCH_TIMEOUT_NORM;

	// B3: possession EMA [0,1]
	obs += self.possessionEMA;

	// B4: boost slope over 1s window [-1,1] (positive = gaining boost)
	float boostSlope = 0.0f;
	if (self.boostHistory.size() >= 2) {
		float dtWin = self.boostHistory.back().first - self.boostHistory.front().first;
		if (dtWin > 0.0f) {
			float dBoost = self.boostHistory.back().second - self.boostHistory.front().second;
			boostSlope = RS_CLAMP(dBoost / dtWin, -1.0f, 1.0f);
		}
	}
	obs += boostSlope;

	// B5: position MAD over 2s window [0,1] (mean distance from centroid)
	float posMad = 0.0f;
	if (self.positionHistory.size() >= 2) {
		Vec mean = self.positionHistory.front().second - self.positionHistory.front().second;
		for (const auto& e : self.positionHistory) mean = mean + e.second;
		mean = mean / (float)self.positionHistory.size();
		float sumDist = 0.0f;
		for (const auto& e : self.positionHistory)
			sumDist += (e.second - mean).Length();
		posMad = RS_CLAMP(sumDist / (float)self.positionHistory.size() / POS_VARIANCE_NORM, 0.0f, 1.0f);
	}
	obs += posMad;

	// B6: max ball Z over 1.5s window [0,1]
	float maxBallZ = 0.0f;
	for (const auto& e : self.ballZHistory)
		maxBallZ = RS_MAX(maxBallZ, e.second);
	obs += RS_CLAMP(maxBallZ / CommonValues::CEILING_Z, 0.0f, 1.0f);

	// B7: mean ball speed over 1s window [0,1]
	float meanBallSpd = 0.0f;
	if (!self.ballSpeedHistory.empty()) {
		float sum = 0.0f;
		for (const auto& e : self.ballSpeedHistory) sum += e.second;
		meanBallSpd = sum / (float)self.ballSpeedHistory.size();
	}
	obs += RS_CLAMP(meanBallSpd / BALL_SPEED_NORM, 0.0f, 1.0f);

	// B8: team avg dist to ball EMA [0,1]
	obs += self.teamAvgDistToBallEMA;
}

RLGC::FList RLGC::AdvancedObsPaddedV2::BuildObs(const Player& player, const GameState& state) {
	FList obs = {};

	bool inv = player.team == Team::ORANGE;

	// Update memory trackers for this player's perspective (warm even when blocks disabled)
	UpdateMemoryTrackers(player, state);

	auto ball = InvertPhys(state.ball, inv);
	auto& pads = state.GetBoostPads(inv);
	auto& padTimers = state.GetBoostPadTimers(inv);

	// =========================================================================
	// BALL STATE
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
	// BOOST PAD STATE
	// =========================================================================
	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		if (pads[i]) {
			obs += 1.f;
		} else {
			obs += 1.f / (1.f + padTimers[i]);
		}
	}

	// =========================================================================
	// SELF
	// =========================================================================
	AddPlayerToObsV2(obs, player, inv, ball);

	constexpr int playerObsSize = PLAYER_OBS_SIZE;

	// =========================================================================
	// TEAMMATES AND OPPONENTS (PADDED + SHUFFLED)
	// =========================================================================
	std::vector<FList> teammates = {}, opponents = {};

	for (const Player& other : state.players) {
		if (other.carId == player.carId)
			continue;

		FList playerObs = {};
		AddPlayerToObsV2(playerObs, other, inv, ball);

		if (other.team == player.team)
			teammates.push_back(playerObs);
		else
			opponents.push_back(playerObs);
	}

	if (teammates.size() > (size_t)(maxPlayers - 1))
		RG_ERR_CLOSE("AdvancedObsPaddedV2: Too many teammates, maximum is " << (maxPlayers - 1));
	if (opponents.size() > (size_t)maxPlayers)
		RG_ERR_CLOSE("AdvancedObsPaddedV2: Too many opponents, maximum is " << maxPlayers);

	// Pad to target sizes
	{
		int targetTeammates = maxPlayers - 1;
		int targetOpponents = maxPlayers;
		while ((int)teammates.size() < targetTeammates)
			teammates.push_back(FList(playerObsSize));
		while ((int)opponents.size() < targetOpponents)
			opponents.push_back(FList(playerObsSize));
	}

	// Shuffle to prevent slot bias
	std::vector<size_t> tmIdx(teammates.size()), opIdx(opponents.size());
	std::iota(tmIdx.begin(), tmIdx.end(), 0);
	std::iota(opIdx.begin(), opIdx.end(), 0);
	std::shuffle(tmIdx.begin(), tmIdx.end(), ::Math::GetRandEngine());
	std::shuffle(opIdx.begin(), opIdx.end(), ::Math::GetRandEngine());

	for (size_t i : tmIdx) obs += teammates[i];
	for (size_t i : opIdx)  obs += opponents[i];

	// =========================================================================
	// MEMORY BLOCKS A+B — always 16 floats (zero-filled when disabled)
	// =========================================================================
	if (memoryBlocksEnabled) {
		AddMemoryBlocksAToObs(obs, player);  // 8 floats: A1–A8
		AddMemoryBlocksBToObs(obs, player);  // 8 floats: B1–B8
	} else {
		for (int i = 0; i < 16; i++) obs += 0.0f;
	}

	return obs;
}
