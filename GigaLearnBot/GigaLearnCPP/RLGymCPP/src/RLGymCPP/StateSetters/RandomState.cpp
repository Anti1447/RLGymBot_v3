/*
 * File: RandomState.cpp
 *
 * Summary of changes from the original ResetArena:
 *   1. Overlap avoidance — car positions are rejection-sampled (up to 32 tries)
 *      against the ball and all previously-placed cars, preventing frame-1
 *      separation impulses that would inject garbage velocity into the first
 *      observations. Falls back to the last candidate if no clear spot is found.
 *   2. Tunable airborne probability — the flat 0.5 coin flip for grounded vs.
 *      airborne is replaced by the `airborneProbability` member (default 0.5),
 *      so aerial-start density can be set per curriculum phase.
 *   3. Grounded velocity aligned to facing — grounded cars now drive forward
 *      along their facing direction at a random speed instead of sliding in a
 *      random horizontal direction. (vel computed after rotMat is built.)
 *   4. Magic number removed — hardcoded car rest height 17 → CommonValues::KICKOFF_CAR_Z.
 *   5. Dead variable removed — the unused `randVelDir` line.
 *   6. Namespace hygiene — RandNormVec() moved into an anonymous namespace.
 *   7. onGround decided before position sampling so the Z-range is constrained
 *      correctly (grounded at KICKOFF_CAR_Z, airborne from CAR_Z_MIN up).
 *
 * Header edits required to compile:
 *   - Add `float airborneProbability = 0.5f;` to the RandomState class.
 *   - Ensure <vector> is included.
 */

#include "RandomState.h"
#include "../Math.h"

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;

namespace {
	Vec RandNormVec() {
		return RLGC::Math::RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
	}
}

void RLGC::RandomState::ResetArena(Arena* arena) {

	// Reset boost pads and everything
	arena->ResetToRandomKickoff();

	// CRITICAL: Restore normal gravity to the ball
	// (In case previous episode was aerial training with zero gravity)
	float gravityBT = RLConst::GRAVITY_Z * UU_TO_BT;
	arena->ball->_rigidBody.setGravity(btVector3(0, 0, gravityBT));

	constexpr float
		X_MAX = 3500,
		Y_MAX = 4000,
		Z_MAX = 1820,
		CAR_Z_MIN = 150,
		PITCH_MAX = M_PI / 2,
		YAW_MAX = M_PI,
		ROLL_MAX = M_PI,
		ANGVEL_MAX = 5.5f;

	// --- Overlap-avoidance tuning -------------------------------------------
	// Rejection sampling keeps random spawns from interpenetrating, which would
	// otherwise trigger an explosive separation impulse on frame 1 and inject
	// garbage velocity into the first observations.
	constexpr float
		CAR_SPAWN_RADIUS = 80.0f,   // conservative car bounding radius
		MIN_CAR_CAR_DIST = 2.0f * CAR_SPAWN_RADIUS,
		MIN_CAR_BALL_DIST = CommonValues::BALL_RADIUS + CAR_SPAWN_RADIUS;
	constexpr int MAX_PLACEMENT_TRIES = 32;

	// Probability a car spawns airborne when carsOnGround == false.
	// Was a flat 0.5 coin flip; expose it so the aerial-start density can be
	// tuned per curriculum phase without touching the rest of the routine.
	const float airborneProb = airborneProbability;  // see header (default ~0.5)

	{ // Randomize ball
		BallState bs = {};
		bs.pos = Math::RandVec(Vec(-X_MAX, -Y_MAX, CommonValues::BALL_RADIUS), Vec(X_MAX, Y_MAX, Z_MAX));
		if (randBallSpeed) {
			bs.vel = RandNormVec() * RandFloat(0, 4000);
			bs.angVel = Math::RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
		}
		arena->ball->SetState(bs);
	}

	const Vec ballPos = arena->ball->GetState().pos;

	// Track already-placed car positions so later cars avoid them.
	std::vector<Vec> placedCarPositions;
	placedCarPositions.reserve(arena->_cars.size());

	for (Car* car : arena->_cars) { // Randomize cars
		CarState cs = {};

		bool onGround = carsOnGround ? true : (RandFloat() < airborneProb ? false : true);

		// --- Position with rejection sampling against ball + other cars ------
		Vec candidate;
		bool placed = false;
		for (int attempt = 0; attempt < MAX_PLACEMENT_TRIES; ++attempt) {
			float zMin = onGround ? CommonValues::KICKOFF_CAR_Z : CAR_Z_MIN;
			candidate = Math::RandVec(Vec(-X_MAX, -Y_MAX, zMin), Vec(X_MAX, Y_MAX, Z_MAX));
			if (onGround)
				candidate.z = CommonValues::KICKOFF_CAR_Z;

			// Reject if too close to the ball.
			if ((candidate - ballPos).Length() < MIN_CAR_BALL_DIST)
				continue;

			// Reject if too close to any already-placed car.
			bool clash = false;
			for (const Vec& other : placedCarPositions) {
				if ((candidate - other).Length() < MIN_CAR_CAR_DIST) {
					clash = true;
					break;
				}
			}
			if (clash) continue;

			placed = true;
			break;
		}
		// If we never found a clear spot, accept the last candidate; the
		// separation impulse on a rare episode is preferable to an infinite loop.
		(void)placed;

		cs.pos = candidate;
		placedCarPositions.push_back(candidate);

		Angle angle = Angle(RandFloat(-YAW_MAX, YAW_MAX), RandFloat(-PITCH_MAX, PITCH_MAX), RandFloat(-ROLL_MAX, ROLL_MAX));

		if (onGround) {
			cs.pos.z = CommonValues::KICKOFF_CAR_Z;
			angle.pitch = angle.roll = 0;
			cs.angVel = {};
		}

		cs.rotMat = angle.ToRotMat();

		if (randCarSpeed) {
			if (onGround) {
				// Grounded cars drive forward along their facing direction at a
				// random speed (no sideways/backward sliding, no vertical or
				// angular velocity). rotMat is set above, so forward is valid.
				cs.vel = cs.rotMat.forward * RandFloat(0, RLConst::CAR_MAX_SPEED);
				cs.vel.z = 0;
			}
			else {
				cs.vel = RandNormVec() * RandFloat(0, RLConst::CAR_MAX_SPEED);
				cs.angVel = RandNormVec() * ANGVEL_MAX;
			}
		}

		cs.boost = RandFloat(0, 100);

		car->SetState(cs);
	}
}