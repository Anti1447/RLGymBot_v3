// ============================================================================
// BallSteerReward.h
// 2026-06-19
//
// Rewards laterally biasing the dribble ball toward the inside of a turn.
// Elite skill: using the car body to steer the ball through curves.
//
// Category:  MECHANICS (DRIBBLING sub-category)
// ZeroSum:   No -- pure self-reward for technique acquisition
// Phase:     4+ (bot already dribbles; now learn to steer through turns)
//
// Gradient conflict with DribbleReward resolved via Option C (delta shaping):
// rewards lateral ball velocity toward turn side, not absolute offset.
// DribbleReward grades state; this reward grades transition. Compatible.
//
// W&B: ball_steer_quality, ball_steer_activation
// ============================================================================
#pragma once
#include "Reward.h"
#include <unordered_map>

namespace RLGC {

class BallSteerReward : public Reward {
public:
    // Carry detection geometry (matches DribbleFlickRewardBase exactly)
    static constexpr float CARRY_UP_MIN         = 30.0f;
    static constexpr float CARRY_UP_MAX         = 170.0f;
    static constexpr float CARRY_RIGHT_MAX      = 110.0f;
    static constexpr float CARRY_FWD_MAX        = 160.0f;
    static constexpr float CARRY_MIN_CAR_SPEED  = 500.0f;

    // Push detection geometry
    static constexpr float PUSH_DIST_MAX         = 300.0f;
    static constexpr float PUSH_FRONT_ALIGN_MIN  = 0.5f;
    static constexpr float PUSH_BALL_Z_MAX       = 150.0f;
    static constexpr float PUSH_LATERAL_MAX      = 200.0f;

    // Turn rate
    static constexpr float MIN_TURN_RATE         = 0.4f;   // rad/s car-local yaw

    // Reward amounts
    static constexpr float BASE_REWARD           = 0.05f;  // == PER_FRAME_CAP by design
    static constexpr float PER_FRAME_CAP         = 0.05f;
    static constexpr float PER_TURN_CAP          = 3.0f;   // resets on direction reversal

    // Grace window
    static constexpr float BREAK_RESET_SECONDS   = 0.5f;

    // Speed quality ramp
    static constexpr float SPEED_RAMP_ZERO       = 200.0f;
    static constexpr float SPEED_RAMP_FULL       = 500.0f;
    static constexpr float SPEED_TAPER_START     = 1410.0f;
    static constexpr float SPEED_TAPER_END       = 2200.0f;
    static constexpr float SPEED_TAPER_FLOOR     = 0.6f;

    // matchQuality ramp half-width (UU/s of lateral relative velocity)
    static constexpr float LATERAL_SOFTMAX       = 80.0f;

    // Kickoff exclusion
    static constexpr float KICKOFF_BALL_DIST     = 280.0f;
    static constexpr float KICKOFF_BALL_SPEED    = 200.0f;

private:
    mutable std::unordered_map<uint32_t, Vec>   _prevForward;
    mutable std::unordered_map<uint32_t, float> _turnBudgetUsed;
    mutable std::unordered_map<uint32_t, int>   _turnSign;
    mutable std::unordered_map<uint32_t, float> _breakTimer;
    float _currentTime = 0.0f;

public:

    virtual void Reset(const GameState& initialState) override {
        _prevForward.clear();
        _turnBudgetUsed.clear();
        _turnSign.clear();
        _breakTimer.clear();
        _currentTime = 0.0f;
    }

    virtual void PreStep(const GameState& state) override {
        // Accumulate time ONCE per step (not per player — CLAUDE.md 7C)
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        _currentTime += dt;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {

        // --- Step 0: Early exits ---
        if (state.ball.pos.Length() < KICKOFF_BALL_DIST &&
            state.ball.vel.Length() < KICKOFF_BALL_SPEED)
            return 0.0f;
        if (player.isDemoed) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        uint32_t id = player.carId;

        // --- Step 1: Ball position in car-local frame ---
        Vec   rel      = state.ball.pos - player.pos;
        float up       = rel.Dot(player.rotMat.up);
        float fwd      = rel.Dot(player.rotMat.forward);
        float right    = rel.Dot(player.rotMat.right);
        float carSpeed = player.vel.Length();

        // --- Step 2: CARRY detection ---
        bool inCarry = (up          >  CARRY_UP_MIN       &&
                        up          <  CARRY_UP_MAX        &&
                        fabs(right) <  CARRY_RIGHT_MAX     &&
                        fabs(fwd)   <  CARRY_FWD_MAX       &&
                        carSpeed    >= CARRY_MIN_CAR_SPEED);

        // --- Step 3: PUSH detection (only if not CARRY) ---
        float ballDist   = rel.Length();
        Vec   toBallNorm = (ballDist > 1.0f) ? rel.Normalized() : Vec(0.0f, 0.0f, 0.0f);
        float frontAlign = player.rotMat.forward.Dot(toBallNorm);

        bool inPush = (!inCarry                                   &&
                       ballDist            <= PUSH_DIST_MAX       &&
                       frontAlign          >= PUSH_FRONT_ALIGN_MIN &&
                       state.ball.pos.z    <  PUSH_BALL_Z_MAX     &&
                       fabs(right)         <  PUSH_LATERAL_MAX    &&
                       carSpeed            >= CARRY_MIN_CAR_SPEED);

        // --- Step 4: Break timer / ball-control gate ---
        // "Ball control lost" = not in CARRY or PUSH zone.
        // Countdown from BREAK_RESET_SECONDS; budget cleared when first hits 0.
        if (inCarry || inPush) {
            _breakTimer[id] = BREAK_RESET_SECONDS;
        } else {
            float& bt = _breakTimer[id];   // lazy-init to 0.0f
            if (bt > 0.0f) {
                bt = RS_MAX(0.0f, bt - dt);  // grace window still open
            } else {
                // Ball control lost beyond grace window: reset all per-turn state
                _turnBudgetUsed[id] = 0.0f;
                _turnSign[id]       = 0;
                _prevForward.erase(id);
                return 0.0f;
            }
        }

        // --- Step 5: Car-local yaw rate (rad/s) ---
        // Project angVel onto car's up axis: correct for all orientations, team-symmetric.
        // Negative = turning right in car frame.
        float turnRateCarLocal = player.angVel.Dot(player.rotMat.up);
        if (fabs(turnRateCarLocal) < MIN_TURN_RATE) {
            _prevForward.erase(id);   // reset so next real turn gets a clean first step
            return 0.0f;
        }

        // --- Step 6: Per-turn cap with direction-reversal reset ---
        bool firstStep = (_prevForward.count(id) == 0);
        _prevForward[id] = player.rotMat.forward;

        int newSign = (turnRateCarLocal > 0.0f) ? 1 : -1;  // +1=left, -1=right
        if (firstStep || _turnSign[id] == 0) {
            _turnSign[id] = newSign;
        } else if (_turnSign[id] != newSign) {
            // Turn direction reversed: fresh budget for the new turn leg
            _turnSign[id]       = newSign;
            _turnBudgetUsed[id] = 0.0f;
        }

        float& budgetUsed = _turnBudgetUsed[id];
        if (budgetUsed >= PER_TURN_CAP) return 0.0f;

        // --- Step 7: matchQuality — delta shaping ---
        // Reward the component of ball lateral velocity (relative to car) that moves
        // toward the inside of the turn. Compatible with DribbleReward's centering:
        // DribbleReward grades the state; this reward grades the transition.
        //   Turn left (turnRate > 0) -> want lateralRelVel < 0 (ball moving left) -> signedMatch > 0
        //   Turn right (turnRate < 0) -> want lateralRelVel > 0 (ball moving right) -> signedMatch > 0
        Vec   relVel        = state.ball.vel - player.vel;
        float lateralRelVel = relVel.Dot(player.rotMat.right);
        float signedMatch   = -turnRateCarLocal * lateralRelVel;

        float matchQuality = RS_CLAMP(signedMatch / LATERAL_SOFTMAX, 0.0f, 1.0f);
        if (matchQuality < 0.01f) return 0.0f;

        // --- Step 8: speedQuality — smooth ramp unified with DribbleReward minCarSpeed ---
        float speedQuality;
        if      (carSpeed <= SPEED_RAMP_ZERO)   { speedQuality = 0.0f; }
        else if (carSpeed <= SPEED_RAMP_FULL)   { speedQuality = (carSpeed - SPEED_RAMP_ZERO) / (SPEED_RAMP_FULL - SPEED_RAMP_ZERO); }
        else if (carSpeed <= SPEED_TAPER_START) { speedQuality = 1.0f; }
        else if (carSpeed <= SPEED_TAPER_END)   {
            float t = (carSpeed - SPEED_TAPER_START) / (SPEED_TAPER_END - SPEED_TAPER_START);
            speedQuality = 1.0f - t * (1.0f - SPEED_TAPER_FLOOR);
        } else                                  { speedQuality = SPEED_TAPER_FLOOR; }

        // --- Step 9: Final reward with frame cap and per-turn budget ---
        float frameReward = BASE_REWARD * matchQuality * speedQuality;
        frameReward = RS_MIN(frameReward, PER_FRAME_CAP);

        float remaining = PER_TURN_CAP - budgetUsed;
        frameReward     = RS_MIN(frameReward, remaining);
        budgetUsed     += frameReward;

        return frameReward;
    }
};

} // namespace RLGC
