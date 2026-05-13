#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"

// ============================================================================
// PASSING REWARDS - Progressive system from Bronze to SSL
// ============================================================================
// Key concepts:
// 1. Bronze-Diamond: Reward back passes (safe, simple)
// 2. Gold-Plat: Reward forward passes with positioning checks
// 3. Diamond+: Reward team positioning and pass reception
//
// References: Passing Guide by Rank (Bronze-SSL)
// ============================================================================

// ============================================================================
// BackPassReward - Diamond/SSL safe passing
// Rewards passes backward to teammates in recovery or rotation
// Key: Safe, maintains possession, minimal risk
// ============================================================================
class BackPassReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastTouchPos;

public:
    virtual void Reset(const GameState& initialState) override {
        lastTouchPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Store touch position for this player
        lastTouchPos[player.carId] = player.pos;

        if (!state.prev) return 0.0f;

        // ====================================================================
        // NEW: Only reward if passer is in OPPONENT'S HALF
        // This ensures we're rewarding tactical retreat from attacking positions
        // ====================================================================
        if (!RewardMath::IsInOpponentHalf(player)) {
            return 0.0f;  // Passer is not in opponent's half - no reward
        }

        // Check if ball moved backward (toward own goal)
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        Vec ownGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        Vec toOwnGoal = (ownGoal - player.pos).Normalized();

        // Back pass = moving toward own goal after touch
        float backPassAlignment = ballMovement.Normalized().Dot(toOwnGoal);

        if (backPassAlignment > 0.5f) {
            // Check if there's a teammate behind to receive
            float minTeammateDistance = 999999.0f;
            bool receiverInOwnHalf = false;

            for (const auto& other : state.players) {
                if (other.carId == player.carId || other.team != player.team) continue;

                float dist = (other.pos - player.pos).Length();
                // Teammate should be behind player
                Vec toTeammate = (other.pos - player.pos).Normalized();
                float alignment = toTeammate.Dot(toOwnGoal);

                if (alignment > 0.3f) {  // Behind enough
                    minTeammateDistance = RS_MIN(minTeammateDistance, dist);

                    // ================================================================
                    // NEW: Check if closest receiver is in OWN HALF
                    // Only consider teammates who are in defensive position
                    // ================================================================
                    if (RewardMath::IsInOwnHalf(other)) {
                        receiverInOwnHalf = true;
                    }
                }
            }

            // ====================================================================
            // NEW: Only reward if receiver is in own half AND within reasonable distance
            // This ensures back passes are to players ready to defend
            // ====================================================================
            if (minTeammateDistance < 2000.0f && receiverInOwnHalf) {  // Reasonable back pass distance
                float distance_factor = 1.0f - RS_CLAMP(minTeammateDistance / 2000.0f, 0.0f, 1.0f);
                return 5.0f * distance_factor;  // 0-5 reward for strategic back pass
            }
        }

        return 0.0f;
    }
};

// ============================================================================
// ForwardPassReward - Gold+ forward/setup passes
// Rewards passes that move ball toward opponent goal with teammate positioning
// Key: Requires teammate awareness, direction control
// ============================================================================
class ForwardPassReward : public Reward {
private:
    static constexpr float MIN_PASS_DISTANCE = 300.0f;  // Lowered from 500
    static constexpr float TEAMMATE_POSITIONING_RANGE = 3000.0f;  // Increased from 2500

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check forward direction (toward opponent goal)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();  // From ball, not player

        // Ball movement analysis
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        float ballDistance = ballMovement.Length();

        // Must move with meaningful distance
        if (ballDistance < MIN_PASS_DISTANCE) return 0.0f;

        // Relaxed forward alignment check (0.1 instead of 0.3)
        float forwardAlignment = ballMovement.Normalized().Dot(toGoal);
        if (forwardAlignment < 0.1f) return 0.0f;

        // Find best teammate ahead of ball
        float bestTeammateDistance = FLT_MAX;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Vector from ball to teammate
            Vec toTeammate = (other.pos - state.ball.pos);
            float distanceToTeammate = toTeammate.Length();

            // Teammate must be "ahead" (toward goal direction): 0.2+ dot product (relaxed from 0.4)
            float aheadAlignment = toTeammate.Normalized().Dot(toGoal);
            if (aheadAlignment > 0.2f && distanceToTeammate < TEAMMATE_POSITIONING_RANGE) {
                bestTeammateDistance = RS_MIN(bestTeammateDistance, distanceToTeammate);
            }
        }

        // Simple distance-based reward (no scoring threshold)
        if (bestTeammateDistance < FLT_MAX) {
            float distance_factor = 1.0f - RS_CLAMP(bestTeammateDistance / TEAMMATE_POSITIONING_RANGE, 0.0f, 1.0f);
            return 6.0f * distance_factor;  // Max: 6.0 (lowered from 8.0)
        }

        return 0.0f;
    }
};

// ============================================================================
// HighPassReward - Plat+ advanced passes (lobs, aerials)
// Rewards high-ball passes that create scoring opportunities
// Key: Ball height + teammate positioning + angle toward goal
// ============================================================================
class HighPassReward : public Reward {
private:
    static constexpr float HIGH_BALL_THRESHOLD = 100.0f;  // Lowered from 200

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Ball movement analysis
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        float ballSpeed = ballMovement.Length();

        // Require minimum ball speed for realistic passes
        if (ballSpeed < 200.0f) return 0.0f;

        // Ball must be elevated after touch
        float heightGain = state.ball.pos.z - state.prev->ball.pos.z;
        if (heightGain < 50.0f) return 0.0f;

        // Lowered ball height threshold (100 instead of 200)
        if (state.ball.pos.z < HIGH_BALL_THRESHOLD) return 0.0f;

        // Check passing direction toward goal (relaxed from 0.5 to 0.3)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballMovement.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;  // Relaxed from 0.5f

        // Find best teammate in aerial reception position
        float bestAerialDistance = FLT_MAX;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Distance from ball to teammate
            Vec toTeammate = (other.pos - state.ball.pos);
            float distance = toTeammate.Length();

            // Teammate must be within aerial range (200-1500 units)
            if (distance < 1500.0f && distance > 200.0f) {
                // FIXED: Correct height calculation - teammate below ball is good
                // Calculate how far below the ball the teammate is
                float heightBelowBall = RS_MAX(0.0f, state.ball.pos.z - other.pos.z);

                // Reward teammate within reasonable catch height (0-400 units below)
                if (heightBelowBall < 400.0f) {
                    bestAerialDistance = RS_MIN(bestAerialDistance, distance);
                }
            }
        }

        if (bestAerialDistance < FLT_MAX) {
            // Simple distance-based calculation
            float distance_factor = 1.0f - RS_CLAMP(bestAerialDistance / 1500.0f, 0.0f, 1.0f);
            return 8.0f * distance_factor;  // Max: 8.0 (lowered from 12.0)
        }

        return 0.0f;
    }
};

// ============================================================================
// OffensivePlayReward - SSL setup passes leading to direct scoring chances
// Rewards creating immediate scoring opportunities through passes
// Key: Teammate in striking position + undefended + high ball speed
// ============================================================================
class OffensivePlayReward : public Reward {
private:
    static constexpr float GOAL_PROXIMITY = 2000.0f;  // Close to enemy goal
    static constexpr float MIN_BALL_SPEED = 500.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Pass must result in significant ball movement
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Check for teammate in striking position
        float bestStrikingScore = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            float distToGoal = (other.pos - opponentGoal).Length();

            // Teammate close to enemy goal (striking position)
            if (distToGoal < GOAL_PROXIMITY) {
                // Check if opponent is blocking
                int blockingOpponents = 0;
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    Vec toTeammate = (other.pos - opp.pos);
                    if (toTeammate.Length() < 500.0f) blockingOpponents++;
                }

                // Undefended or outnumbering defenders
                if (blockingOpponents <= 1) {
                    float positionScore = 1.0f - RS_CLAMP(distToGoal / GOAL_PROXIMITY, 0.0f, 1.0f);
                    bestStrikingScore = RS_MAX(bestStrikingScore, positionScore);
                }
            }
        }

        if (bestStrikingScore > 0.3f) {
            return 15.0f * bestStrikingScore;  // 0-15 reward for offensive setups
        }

        return 0.0f;
    }
};

// ============================================================================
// InfieldPassReward — Champion+ lateral "switch" pass to teammate at similar Y
// 2026-03-08 | Rev 1.0 | NJH
//
// An infield pass moves the ball primarily in X (lateral) to a teammate at
// roughly the same field-depth (Y) as the passer. The pass should place the
// ball ahead of the receiver — between the receiver and the opponent goal,
// closer to the receiver than to the goal.
//
// Gate conditions (all must pass):
//   - ballTouchedStep
//   - 2v2+ (players.size() >= 4)
//   - abs(ball.vel.x) > abs(ball.vel.y)   — X-dominant direction
//   - ball speed >= MIN_BALL_SPEED (400 UU/s)
//
// Per receiver (best score wins):
//   - Teammate Y within Y_MATCH_THRESHOLD (1500 UU) of passer
//   - Ball velocity (XY) aimed toward receiver: dot >= DIR_THRESHOLD (0.25)
//   - vel.x sign matches (receiver.x - ball.x) — ball heading toward receiver's X
//   - t = (receiver.x - ball.x) / vel.x in (0, MAX_T=5s]
//   - projY = ball.y + vel.y * t must be between receiver.y and goal.y
//     (ball lands ahead of receiver, not behind or in the goal)
//
// Score = dotToReceiver * yMatchQuality * placementQuality
//   placementQuality: triangle peak at attackProgress=0.2 (just in front of receiver)
// Reward = MAX_REWARD (5.0) * score
// ============================================================================
class InfieldPassReward : public Reward {
private:
    static constexpr float Y_MATCH_THRESHOLD = 1500.0f; // passer-receiver Y gap max
    static constexpr float DIR_THRESHOLD     = 0.25f;   // min XY dot toward receiver
    static constexpr float MIN_BALL_SPEED    = 400.0f;  // UU/s
    static constexpr float MAX_T             = 5.0f;    // max time-of-flight to project
    static constexpr float MAX_REWARD        = 5.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if ((int)state.players.size() < 4) return 0.0f;

        // X must dominate Y in ball direction
        float absVx = std::abs(state.ball.vel.x);
        float absVy = std::abs(state.ball.vel.y);
        if (absVx <= absVy) return 0.0f;

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Goal Y for "ahead of receiver" check
        float goalY = (player.team == Team::BLUE)
            ?  CommonValues::GOAL_LINE_Y   // +5120
            : -CommonValues::GOAL_LINE_Y;  // -5120

        // Ball velocity in XY plane (normalised)
        Vec ballVel2D = Vec(state.ball.vel.x, state.ball.vel.y, 0.0f);
        float bv2DLen = ballVel2D.Length();
        if (bv2DLen < 1.0f) return 0.0f;
        Vec ballVelNorm2D = Vec(ballVel2D.x / bv2DLen, ballVel2D.y / bv2DLen, 0.0f);

        const Player* bestReceiver = nullptr;
        float bestScore = -1.0f;

        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team)   continue;
            if (other.isDemoed)              continue;

            // Y-match: receiver must be at similar field depth as passer
            float yDelta = std::abs(other.pos.y - player.pos.y);
            if (yDelta > Y_MATCH_THRESHOLD) continue;
            float yMatchQuality = 1.0f - (yDelta / Y_MATCH_THRESHOLD);

            // Direction: ball XY velocity aimed toward receiver
            Vec toReceiver2D = Vec(other.pos.x - state.ball.pos.x,
                                   other.pos.y - state.ball.pos.y, 0.0f);
            float recDist2D = toReceiver2D.Length();
            if (recDist2D < 50.0f) continue;
            Vec recNorm2D = Vec(toReceiver2D.x / recDist2D, toReceiver2D.y / recDist2D, 0.0f);
            float dotToReceiver = ballVelNorm2D.x * recNorm2D.x + ballVelNorm2D.y * recNorm2D.y;
            if (dotToReceiver < DIR_THRESHOLD) continue;

            // Time-of-flight to receiver's X (ball must be heading toward that X)
            float xDiff = other.pos.x - state.ball.pos.x;
            if (state.ball.vel.x == 0.0f) continue;
            if ((xDiff > 0.0f) != (state.ball.vel.x > 0.0f)) continue; // wrong direction
            float t = xDiff / state.ball.vel.x;
            if (t <= 0.0f || t > MAX_T) continue;

            // Where does the ball land in Y at that time?
            float projY = state.ball.pos.y + state.ball.vel.y * t;

            // "Between receiver and goal, closer to receiver"
            float span = goalY - other.pos.y; // distance receiver→goal along Y
            if (std::abs(span) < 50.0f) continue;
            bool landedAhead, landedShortOfGoal;
            if (player.team == Team::BLUE) {
                landedAhead     = projY > other.pos.y;
                landedShortOfGoal = projY < CommonValues::GOAL_LINE_Y;
            } else {
                landedAhead     = projY < other.pos.y;
                landedShortOfGoal = projY > -CommonValues::GOAL_LINE_Y;
            }
            if (!landedAhead || !landedShortOfGoal) continue;

            float attackProgress = RS_CLAMP(
                (player.team == Team::BLUE)
                    ? (projY - other.pos.y) / span
                    : (other.pos.y - projY) / std::abs(span),
                0.0f, 1.0f
            );

            // Triangle quality: peaks at 0.2 (just ahead of receiver), falls off toward 0 and 1
            float placementQuality;
            if (attackProgress <= 0.2f) {
                placementQuality = attackProgress / 0.2f;
            } else {
                placementQuality = 1.0f - (attackProgress - 0.2f) / 0.8f;
            }
            placementQuality = RS_CLAMP(placementQuality, 0.0f, 1.0f);

            float score = dotToReceiver * yMatchQuality * placementQuality;
            if (score > bestScore) {
                bestScore    = score;
                bestReceiver = &other;
            }
        }

        if (bestScore <= 0.0f || !bestReceiver) return 0.0f;
        return MAX_REWARD * bestScore;
    }
};

// ============================================================================
// DownfieldPassReward — Champion+ long clearance from own half to attacking
// teammate 2000–6000 Y units downfield.
// 2026-03-08 | Rev 1.0 | NJH
//
// A downfield pass is a kick from the defensive half to a receiver already
// in the attacking half, with the ball landing near the receiver on its way
// to the opponent goal (ball "between receiver and goal, closer to receiver").
//
// Gate conditions (all must pass):
//   - ballTouchedStep
//   - 2v2+ (players.size() >= 4)
//   - ball speed >= MIN_BALL_SPEED (500 UU/s)
//   - Passer in own half (Blue: player.pos.y < 0, Orange: player.pos.y > 0)
//   - Ball forward component >= FORWARD_THRESHOLD (0.4 × ballSpeed)
//
// Per receiver (best score wins):
//   - Receiver in attacking half
//   - Y-separation (receiver attackY − passer attackY) in [2000, 6000]
//   - Ball velocity aimed toward receiver: dot >= DIR_THRESHOLD (0.2)
//   - t = (receiver.y − ball.y) / ball.vel.y in (0, MAX_T=6s]
//   - X deviation at landing (ball.x + vel.x*t vs receiver.x) < X_PROX_RANGE (1500)
//
// Score = forwardComponent * distQuality * dirDot * xProxScore
//   distQuality: triangle peak at IDEAL_Y_SEP=4000 UU
// Reward = MAX_REWARD (7.0) * score
// ============================================================================
class DownfieldPassReward : public Reward {
private:
    static constexpr float MIN_Y_SEP         = 2000.0f;
    static constexpr float IDEAL_Y_SEP       = 4000.0f;
    static constexpr float MAX_Y_SEP         = 6000.0f;
    static constexpr float FORWARD_THRESHOLD = 0.4f;   // fraction of ballSpeed
    static constexpr float DIR_THRESHOLD     = 0.2f;   // min dot toward receiver
    static constexpr float X_PROX_RANGE      = 1500.0f; // max X deviation at landing
    static constexpr float MIN_BALL_SPEED    = 500.0f;
    static constexpr float MAX_T             = 6.0f;
    static constexpr float MAX_REWARD        = 7.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if ((int)state.players.size() < 4) return 0.0f;

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Passer must be in own half
        bool inOwnHalf = (player.team == Team::BLUE)
            ? (player.pos.y < 0.0f)
            : (player.pos.y > 0.0f);
        if (!inOwnHalf) return 0.0f;

        // Ball must have significant forward (attacking) velocity component
        float forwardBallVel = (player.team == Team::BLUE)
            ?  state.ball.vel.y
            : -state.ball.vel.y;
        float forwardComponent = forwardBallVel / ballSpeed; // [-1, 1]
        if (forwardComponent < FORWARD_THRESHOLD) return 0.0f;

        // Ball velocity in XY (for direction dot)
        Vec ballVel2D = Vec(state.ball.vel.x, state.ball.vel.y, 0.0f);
        float bv2DLen = ballVel2D.Length();
        if (bv2DLen < 1.0f) return 0.0f;
        Vec ballVelNorm2D = Vec(ballVel2D.x / bv2DLen, ballVel2D.y / bv2DLen, 0.0f);

        // Passer attack-axis Y (negative for Blue in own half, positive for Orange in own half)
        float passerAttackY = (player.team == Team::BLUE)
            ?  player.pos.y
            : -player.pos.y;

        const Player* bestReceiver = nullptr;
        float bestScore = -1.0f;

        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team)   continue;
            if (other.isDemoed)              continue;

            // Receiver must be in attacking half
            bool recInAttack = (player.team == Team::BLUE)
                ? (other.pos.y > 0.0f)
                : (other.pos.y < 0.0f);
            if (!recInAttack) continue;

            // Y-separation in attack axis
            float receiverAttackY = (player.team == Team::BLUE)
                ?  other.pos.y
                : -other.pos.y;
            float ySep = receiverAttackY - passerAttackY; // always positive (receiver ahead)
            if (ySep < MIN_Y_SEP || ySep > MAX_Y_SEP) continue;

            // Direction dot toward receiver
            Vec toReceiver2D = Vec(other.pos.x - state.ball.pos.x,
                                   other.pos.y - state.ball.pos.y, 0.0f);
            float recDist2D = toReceiver2D.Length();
            if (recDist2D < 50.0f) continue;
            Vec recNorm2D = Vec(toReceiver2D.x / recDist2D, toReceiver2D.y / recDist2D, 0.0f);
            float dirDot = ballVelNorm2D.x * recNorm2D.x + ballVelNorm2D.y * recNorm2D.y;
            if (dirDot < DIR_THRESHOLD) continue;

            // Time for ball to cross receiver's Y
            float yDiffToCross = other.pos.y - state.ball.pos.y;
            if (std::abs(state.ball.vel.y) < 1.0f) continue;
            if ((yDiffToCross > 0.0f) != (state.ball.vel.y > 0.0f)) continue; // wrong direction
            float t = yDiffToCross / state.ball.vel.y;
            if (t <= 0.0f || t > MAX_T) continue;

            // X-proximity when ball crosses receiver's Y
            float ballXAtT = state.ball.pos.x + state.ball.vel.x * t;
            float xDeviation = std::abs(ballXAtT - other.pos.x);
            if (xDeviation > X_PROX_RANGE) continue;
            float xProxScore = 1.0f - RS_CLAMP(xDeviation / X_PROX_RANGE, 0.0f, 1.0f);

            // Distance quality: triangle peaking at IDEAL_Y_SEP
            float distQuality;
            if (ySep <= IDEAL_Y_SEP) {
                distQuality = (ySep - MIN_Y_SEP) / (IDEAL_Y_SEP - MIN_Y_SEP);
            } else {
                distQuality = 1.0f - (ySep - IDEAL_Y_SEP) / (MAX_Y_SEP - IDEAL_Y_SEP);
            }
            distQuality = RS_CLAMP(distQuality, 0.0f, 1.0f);

            float score = forwardComponent * distQuality * dirDot * xProxScore;
            if (score > bestScore) {
                bestScore    = score;
                bestReceiver = &other;
            }
        }

        if (bestScore <= 0.0f || !bestReceiver) return 0.0f;
        return MAX_REWARD * bestScore;
    }
};

// ============================================================================
// PressureEscapeBackPassReward — Phase 6 possession reset under pressure
// 2026-05-01 | NJH
//
// Rewards a deliberate back pass from the opponent's half to an open teammate
// (no opponent within OPEN_MAN_RADIUS of receiver) who is behind the passer
// toward their own goal. Scales up when the passer is under pressure (opponent
// nearby) and/or low on boost.
//
// Teaches: when cornered in the attacking half, find the open man and reset
// possession rather than forcing a bad play or surrendering the ball.
//
// Gates: ballTouchedStep, 2v2+, passer in opponent's half,
//        ball velocity toward own goal (dot > 0.35), ball speed > 300 UU/s,
//        best receiver is behind passer + uncontested + within 600–5000 UU.
//
// Multipliers (stackable):
//   PRESSURE_MULT (×1.5) — opponent within 1800 UU of passer
//   LOW_BOOST_MULT (×1.4) — passer boost < 33
//
// Max reward: 5.0 × 1.5 × 1.4 = 10.5 (rare full-stack case)
// ZeroSum wrapped in RewardConfig at Phase 6.
// ============================================================================
class PressureEscapeBackPassReward : public Reward {
private:
    static constexpr float BACK_PASS_DOT_MIN   = 0.35f;   // ball vel component toward own goal
    static constexpr float MIN_BALL_SPEED      = 300.0f;  // UU/s — filters dribble nudges
    static constexpr float BEHIND_DOT_MIN      = 0.25f;   // receiver must be this "behind" passer
    static constexpr float MIN_RECEIVER_DIST   = 600.0f;
    static constexpr float MAX_RECEIVER_DIST   = 5000.0f;
    static constexpr float OPEN_MAN_RADIUS     = 2000.0f; // no opp within this of receiver = open
    static constexpr float PRESSURE_RADIUS     = 1800.0f; // opp within this of passer = under pressure
    static constexpr float LOW_BOOST_THRESHOLD = 33.0f;
    static constexpr float PRESSURE_MULT       = 1.5f;
    static constexpr float LOW_BOOST_MULT      = 1.4f;
    static constexpr float BASE_REWARD         = 5.0f;

    inline static std::atomic<int>       s_fireCount{0};
    inline static std::atomic<int>       s_pressureFires{0};
    inline static std::atomic<int>       s_lowBoostFires{0};
    inline static std::atomic<long long> s_rewardMilli{0};

public:
    struct Metrics {
        int    fireCount     = 0;
        int    pressureFires = 0;
        int    lowBoostFires = 0;
        double totalReward   = 0.0;
    };
    static Metrics GetAndResetMetrics() {
        return {
            s_fireCount.exchange(0),
            s_pressureFires.exchange(0),
            s_lowBoostFires.exchange(0),
            double(s_rewardMilli.exchange(0)) / 1000.0
        };
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if ((int)state.players.size() < 4) return 0.0f;

        if (!RewardMath::IsInOpponentHalf(player)) return 0.0f;

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Ball must be moving toward own goal (back pass)
        Vec ownGoalBack = (player.team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_BACK
            : CommonValues::ORANGE_GOAL_BACK;
        Vec toOwnGoal = (ownGoalBack - player.pos).Normalized();
        if (state.ball.vel.Normalized().Dot(toOwnGoal) < BACK_PASS_DOT_MIN) return 0.0f;

        // Find best open teammate who is behind the passer
        float bestOpenScore = 0.0f;

        for (const auto& other : state.players) {
            if (other.carId == player.carId) continue;
            if (other.team != player.team)   continue;
            if (other.isDemoed)              continue;

            Vec toReceiver = other.pos - player.pos;
            float dist = toReceiver.Length();
            if (dist < MIN_RECEIVER_DIST || dist > MAX_RECEIVER_DIST) continue;

            // Receiver must be behind the passer (toward own goal)
            if (toReceiver.Normalized().Dot(toOwnGoal) < BEHIND_DOT_MIN) continue;

            // Receiver must be uncontested
            float nearestOppToReceiver = FLT_MAX;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                float d = (opp.pos - other.pos).Length();
                if (d < nearestOppToReceiver) nearestOppToReceiver = d;
            }
            if (nearestOppToReceiver < OPEN_MAN_RADIUS) continue;

            // Openness score: how far the nearest opponent is beyond the open radius
            float openScore = RS_CLAMP(
                (nearestOppToReceiver - OPEN_MAN_RADIUS) / OPEN_MAN_RADIUS, 0.0f, 1.0f
            );
            if (openScore > bestOpenScore) bestOpenScore = openScore;
        }

        if (bestOpenScore <= 0.0f) return 0.0f;

        // Base: 2.5 at minimum openness, 5.0 at fully open
        float reward = BASE_REWARD * (0.5f + 0.5f * bestOpenScore);

        // Pressure bonus: opponent is close to the passer
        bool underPressure = false;
        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.isDemoed) continue;
            if ((opp.pos - player.pos).Length() < PRESSURE_RADIUS) {
                underPressure = true;
                break;
            }
        }
        if (underPressure) {
            reward *= PRESSURE_MULT;
            s_pressureFires.fetch_add(1);
        }

        // Low boost bonus
        if (player.boost < LOW_BOOST_THRESHOLD) {
            reward *= LOW_BOOST_MULT;
            s_lowBoostFires.fetch_add(1);
        }

        s_fireCount.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        return reward;
    }
};
