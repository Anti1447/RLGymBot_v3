#pragma once
#include "../Gamestates/Player.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <cmath>

namespace RLGC {

// ============================================================================
// POSITION ANALYSIS HELPERS - For Overcommitment Detection
// ============================================================================
// Team-aware position checking utilities for Phase 1-3 overcommitment penalties
// Used by SimpleOvercommitmentPenalty, AdvancedOvercommitmentPenalty, etc.
// ============================================================================

class PositionAnalysis {
public:
    // ===== ZONE DETECTION =====

    static bool IsKickoffFormation(const Player& player, const GameState& state) {
        // Check if player is in kickoff formation (all players equidistant from center)
        // This happens at the start of each game for ~5 seconds

        // Simple heuristic: all players near field center
        Vec fieldCenter = Vec(0, 0, 17.01f);
        float distFromCenter = (player.pos - fieldCenter).Length();

        // Kickoff positions are roughly 3500 units from center, but allow some variance
        // Also check game time is early (< 5 seconds suggests kickoff)
        // For now, use conservative distance check
        return distFromCenter < 4000.0f;
    }

    static bool InOpponentCorner(const Player& player) {
        // Check if player is in opponent's corner
        // Corners are high-risk, sticky zones with low escape routes

        const float CORNER_X = 3500.0f;   // Near side walls
        const float CORNER_Y = 4000.0f;   // Deep into opponent half

        // Check if in corner zone
        bool isCorner = (std::abs(player.pos.x) > CORNER_X) &&
                       (std::abs(player.pos.y) > CORNER_Y);

        // Check if in opponent half (team-aware)
        bool isOppHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                        (player.team == Team::ORANGE && player.pos.y < 0);

        return isCorner && isOppHalf;
    }

    static bool InOpponentGoalArea(const Player& player) {
        // Check if player is in/near opponent goal
        // Goal area dimensions match arena

        const float GOAL_WIDTH = 900.0f;
        const float GOAL_LINE_Y = 5120.0f;
        const float BACK_NET_Y = 6000.0f;
        const float GOAL_HEIGHT = 843.0f;

        bool inGoalX = std::abs(player.pos.x) < GOAL_WIDTH;

        // Check Y range (team-aware)
        bool inGoalY;
        if (player.team == Team::BLUE) {
            // Blue team attacks positive Y
            inGoalY = player.pos.y > GOAL_LINE_Y && player.pos.y < BACK_NET_Y;
        } else {
            // Orange team attacks negative Y
            inGoalY = player.pos.y < -GOAL_LINE_Y && player.pos.y > -BACK_NET_Y;
        }

        bool inGoalZ = player.pos.z < GOAL_HEIGHT + 200.0f;

        return inGoalX && inGoalY && inGoalZ;
    }

    static bool InDefensivePosition(const Player& player) {
        // Check if player is in defensive half (defending their own goal)
        // Defensive = closer to own goal than center line

        if (player.team == Team::BLUE) {
            return player.pos.y < 0;
        } else {
            return player.pos.y > 0;
        }
    }

    // ===== THREAT DETECTION =====

    static bool IsDodgingImminentDemo(const Player& player,
                                     const std::vector<Player>& opponents) {
        // Check if an opponent is about to demo/bump this player
        // If true, using flip/jump to dodge is JUSTIFIED

        const float APPROACH_THRESHOLD = 1500.0f;  // Opponent velocity toward us
        const float TIME_TO_CONTACT_MAX = 0.5f;    // Imminent = within 0.5 seconds

        for (const auto& opponent : opponents) {
            if (opponent.team == player.team) continue;  // Only check opponents

            // Calculate opponent's velocity component toward this player
            Vec dirToPlayer = (player.pos - opponent.pos).Normalized();
            float velTowardPlayer = opponent.vel.Dot(dirToPlayer);

            // Is opponent approaching fast enough?
            if (velTowardPlayer > APPROACH_THRESHOLD) {
                float distToOpponent = (player.pos - opponent.pos).Length();
                float timeToContact = distToOpponent / velTowardPlayer;

                // Is contact imminent?
                if (timeToContact > 0 && timeToContact < TIME_TO_CONTACT_MAX) {
                    return true;  // Demo is coming!
                }
            }
        }

        return false;
    }

    // ===== TEAMMATE COVERAGE =====

    static bool TeamHasGoalSideCoverage(const Player& player,
                                       const std::vector<Player>& allPlayers) {
        // Check if a teammate is positioned to defend our goal
        // "Goal side" = between ball and our goal

        // Get our goal position (team-aware)
        Vec ourGoal;
        if (player.team == Team::BLUE) {
            ourGoal = Vec(0, -5120, 0);
        } else {
            ourGoal = Vec(0, 5120, 0);
        }

        // Look for any teammate closer to our goal than this player
        float ourDistToGoal = (player.pos - ourGoal).Length();

        for (const auto& teammate : allPlayers) {
            if (teammate.team != player.team) continue;           // Only check teammates
            if (teammate.carId == player.carId) continue;         // Exclude self

            float theirDistToGoal = (teammate.pos - ourGoal).Length();

            // Is teammate closer to goal AND in defensive position?
            bool theirCloser = theirDistToGoal < ourDistToGoal;
            bool theirInDefense = (teammate.team == Team::BLUE && teammate.pos.y < player.pos.y) ||
                                 (teammate.team == Team::ORANGE && teammate.pos.y > player.pos.y);

            if (theirCloser && theirInDefense) {
                return true;  // Teammate has defensive coverage
            }
        }

        return false;
    }

    // ===== SHOT DETECTION =====

    static bool RecentlyAttemptedShot(const Player& player, float windowSeconds) {
        // Check if player recently attempted a shot/flick/aerial
        // Heuristic: was airborne recently and now landed with low velocity

        if (!player.prev) return false;

        // Was player airborne previously?
        bool wasAirborne = player.prev->isOnGround == false;

        // Is player on ground now with low vertical velocity?
        bool isNowLow = player.isOnGround && std::abs(player.vel.z) < 100.0f;

        // Simple check: transition from airborne to low ground state
        // This catches most shot/flick attempts
        return wasAirborne && isNowLow;
    }

    // ===== DISTANCE CALCULATIONS =====

    static float DistanceBetween(const Vec& a, const Vec& b) {
        return (a - b).Length();
    }

    static float ClosestTeammateDistance(const Player& player,
                                        const Vec& targetPos,
                                        const std::vector<Player>& allPlayers) {
        float closest = FLT_MAX;

        for (const auto& other : allPlayers) {
            if (other.team != player.team) continue;           // Only teammates
            if (other.carId == player.carId) continue;         // Exclude self

            float dist = (other.pos - targetPos).Length();
            closest = RS_MIN(closest, dist);
        }

        return closest;
    }

    static float ClosestOpponentDistance(const Player& player,
                                        const Vec& targetPos,
                                        const std::vector<Player>& allPlayers) {
        float closest = FLT_MAX;

        for (const auto& other : allPlayers) {
            if (other.team == player.team) continue;          // Only opponents

            float dist = (other.pos - targetPos).Length();
            closest = RS_MIN(closest, dist);
        }

        return closest;
    }

    // ===== TRAJECTORY PROJECTION (Phase 2 - Advanced Overcommitment) =====

    static inline Vec projectMotion(const Vec& pos, const Vec& vel, float time) {
        // Project motion vector forward in time (no gravity)
        return pos + vel * time;
    }

    static inline float getRemainingFlipDuration(const Player& player) {
        // Calculate remaining flip/double jump duration based on jump state
        // Flip typically lasts 0.3-0.5 seconds; use jump counter state
        if (player.hasDoubleJumped) return 0.0f;  // No flip available
        if (player.isFlipping) {
            // Flip in progress - estimate ~0.2s remaining
            // In real implementation, would track flip start time
            return 0.2f;
        }
        return 0.0f;
    }

    static inline Vec projectBoostDepletionPosition(const Player& player,
                                                    float boostBurnRate = 33.3f) {
        // Project where player will be when boost runs out at current burn rate
        if (player.boost <= 0.0f) return player.pos;

        float timeToEmpty = player.boost / boostBurnRate;
        return projectMotion(player.pos, player.vel, timeToEmpty);
    }

    static inline float calculateTimeToGround(const Player& player) {
        // Calculate time until player reaches ground (including gravity)
        if (player.isOnGround) return 0.0f;

        float gravity = -650.0f;  // Rocket League gravity constant
        float vy = player.vel.z;
        float pos_z = player.pos.z;

        // Solve: 0 = pos_z + vy*t + 0.5*g*t^2
        float discriminant = vy * vy - 2.0f * gravity * pos_z;
        if (discriminant < 0) return 0.0f;

        float t1 = (-vy - std::sqrt(discriminant)) / gravity;
        return (t1 > 0) ? t1 : 0.0f;
    }

    static inline float projectZPosition(const Player& player, float time) {
        // Get estimated Z position at a future time (with gravity included)
        float gravity = -650.0f;
        return player.pos.z + player.vel.z * time + 0.5f * gravity * time * time;
    }

    static inline bool canFlipReachDistance(const Player& player,
                                           float targetDistance,
                                           const Vec& targetPos) {
        // Check if flip can reach a target distance
        if (!player.isFlipping && player.hasDoubleJumped) return false;  // No flip available

        Vec flipPos = projectMotion(player.pos, player.vel, getRemainingFlipDuration(player));
        float distance = (targetPos - flipPos).Length();

        return distance < targetDistance + 200.0f;  // 200 UU margin for recovery
    }

    static inline Vec predictFlipCompletionPosition(const Player& player) {
        // Project where player will be when current flip completes
        float flipDuration = getRemainingFlipDuration(player);
        return projectMotion(player.pos, player.vel, flipDuration);
    }

    static inline float getAerialTouchableHeight(const Player& player,
                                                const Vec& ballPos) {
        // Calculate projected Z height when player reaches ball's XY position
        // Useful for detecting if aerial will actually reach ball height

        // Estimate time to reach ball XY position
        Vec playerToballXY = Vec(ballPos.x - player.pos.x, ballPos.y - player.pos.y, 0.0f);
        float xyDistance = playerToballXY.Length();

        if (xyDistance < 1.0f) return player.pos.z;  // Already at ball XY

        // Estimate horizontal velocity (assume airborne, mostly horizontal)
        Vec playerVelXY = Vec(player.vel.x, player.vel.y, 0.0f);
        float horizontalSpeed = playerVelXY.Length();

        if (horizontalSpeed < 1.0f) return projectZPosition(player, xyDistance / 1.0f);

        float timeToReach = xyDistance / horizontalSpeed;
        return projectZPosition(player, timeToReach);
    }

    static inline bool isProjectedToStrandInOpponentHalf(const Player& player,
                                                         float boostBurnRate = 33.3f) {
        // Check if boost depletion endpoint is stranded in opponent half
        if (player.boost <= 0.0f) {
            // Already stranded
            bool inOppHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                           (player.team == Team::ORANGE && player.pos.y < 0);
            return inOppHalf;
        }

        Vec depletionPos = projectBoostDepletionPosition(player, boostBurnRate);
        bool inOppHalf = (player.team == Team::BLUE && depletionPos.y > 0) ||
                        (player.team == Team::ORANGE && depletionPos.y < 0);

        // Also check if projected position is far from own goal
        Vec ownGoal = (player.team == Team::BLUE) ? Vec(0, -5120, 0) : Vec(0, 5120, 0);
        float distFromGoal = (depletionPos - ownGoal).Length();

        return inOppHalf && distFromGoal > 3000.0f;
    }
};

}  // namespace RLGC
