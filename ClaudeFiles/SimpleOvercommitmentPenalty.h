// ============================================================================
// PHASE 1 OVERCOMMITMENT PENALTIES: IMPLEMENTATION GUIDE
// ============================================================================
// For RLGymBot v3 - Phase 4 (Platinum) Training
// 
// This reward class teaches resource management and positioning discipline
// while respecting legitimate tactical decisions (demos, contests, shots).
//
// Estimated cost: 5-7 vector/distance calculations every 0.25 seconds
// Expected benefit: Cleaner positioning, less wasted flips, better rotations
// ============================================================================

#pragma once
#include "Framework.h"

namespace RLGC {

class SimpleOvercommitmentPenalty : public Reward {
public:
    float GetReward(Agent* agent) override {
        // Don't call every frame - use sampling
        static float lastCheckTime = 0.0f;
        const float CHECK_INTERVAL = 0.25f;
        
        if (agent->gameState.elapsedTime - lastCheckTime < CHECK_INTERVAL) {
            return 0.0f;
        }
        lastCheckTime = agent->gameState.elapsedTime;
        
        float penalty = 0.0f;
        const Player& player = agent->gameState.players[agent->playerIndex];
        
        // ===== DISTANCE CALCULATIONS (ONE TIME) =====
        float ourDistance = distance(player.pos, agent->gameState.ball.pos);
        
        float closestOpponentDistance = FLT_MAX;
        float closestTeammateDistance = FLT_MAX;
        
        for (const auto& other : agent->gameState.players) {
            float dist = distance(other.pos, agent->gameState.ball.pos);
            
            if (other.carId == player.carId) continue;
            
            if (other.team == player.team) {
                closestTeammateDistance = RS_MIN(closestTeammateDistance, dist);
            } else {
                closestOpponentDistance = RS_MIN(closestOpponentDistance, dist);
            }
        }
        
        // ===== PRE-CHECK EXCEPTIONS =====
        const float BALL_CONTEST_MARGIN = 100.0f;
        const float TEAMMATE_DEFER_MARGIN = 200.0f;
        
        // Exception: We're closest to ball (within margin)
        if (ourDistance <= closestOpponentDistance + BALL_CONTEST_MARGIN) {
            return 0.0f;
        }
        
        // Exception: In kickoff formation
        if (IsKickoffFormation(player)) {
            return 0.0f;
        }
        
        // Exception: Teammate is handling it
        if (closestTeammateDistance < ourDistance - TEAMMATE_DEFER_MARGIN) {
            return 0.0f;
        }
        
        // ===== GATE 1: FLIP IN DANGER =====
        if (player.isFlipping && player.boost < 20.0f) {
            float distToBall = distance(player.pos, agent->gameState.ball.pos);
            
            if (distToBall > 1000.0f) {
                // Flipped far from ball with low boost
                
                // Exception: Opponent is close (active contest)
                if (closestOpponentDistance < 300.0f) {
                    // Active fight, skip
                } else if (IsDodgingImminentDemo(player, agent->gameState.players)) {
                    // Dodging demo, skip
                } else {
                    penalty += 10.0f;
                }
            }
        }
        
        // ===== GATE 2: DOUBLE JUMP DEPLETION =====
        if (player.doubleJumpUsed && player.boost < 30.0f) {
            bool inDefense = player.pos.y < 0;  // Adjust based on team
            
            if (!inDefense) {
                // Exception: Active contest
                if (closestOpponentDistance >= 300.0f || 
                    ourDistance >= closestOpponentDistance + 100.0f) {
                    
                    // Exception: Dodging demo
                    if (!IsDodgingImminentDemo(player, agent->gameState.players)) {
                        penalty += 8.0f;
                    }
                }
            }
        }
        
        // ===== GATE 3: STRANDED IN OPPONENT HALF =====
        if (player.pos.y > 0 && player.boost < 25.0f) {  // Opponent half, low boost
            // Exception: Pressuring opponent
            float distToClosestOpponent = FLT_MAX;
            for (const auto& opp : agent->gameState.players) {
                if (opp.team != player.team) {
                    distToClosestOpponent = RS_MIN(
                        distToClosestOpponent, 
                        distance(player.pos, opp.pos)
                    );
                }
            }
            
            if (distToClosestOpponent >= 500.0f) {
                // Not pressuring
                // Exception: Teammate has defense covered
                if (!TeamHasGoalSideCoverage(player, agent->gameState.players)) {
                    // Check if we have rotation path (too expensive, skip for now)
                    penalty += 5.0f;
                }
            }
        }
        
        // ===== GATE 4: CORNER CHALLENGE RISK =====
        if (InOpponentCorner(player.pos)) {
            // In opponent corner
            if (ourDistance > closestOpponentDistance + BALL_CONTEST_MARGIN) {
                // We're not closest to ball
                if (closestOpponentDistance >= 800.0f) {
                    // Opponent not engaged in corner, we shouldn't be either
                    penalty += 12.0f;
                }
            }
        }
        
        // ===== GATE 5: GOAL NET LANDING TRAP =====
        if (InOpponentGoalArea(player.pos) && player.boost < 50.0f) {
            // In goal area with low boost - dangerous!
            
            // Exception: Did we just attempt a shot?
            if (RecentlyAttemptedShot(player, 0.5f)) {
                // Recent aerial/flick, being in goal expected
                float timeToRecover = (50.0f - player.boost) / 33.3f;  // BOOST_CONSUMPTION_RATE
                
                if (timeToRecover >= 3.0f) {
                    // Takes too long to recover, teammates vulnerable
                    penalty += 15.0f;
                }
                // Otherwise allow it (quick recovery)
            } else {
                // Stuck in goal without good reason
                penalty += 14.0f;
            }
        }
        
        return -penalty;  // Negative = penalty
    }

private:
    // ===== HELPER FUNCTIONS =====
    
    bool IsKickoffFormation(const Player& p) {
        // Check if player position matches any kickoff formation
        // Implementation: compare against CommonValues::KICKOFF_* positions
        // For now, simple heuristic: all players near center at match start
        
        float distFromCenter = distance(p.pos, Vec(0, 0, 17.01f));
        return distFromCenter < 3500.0f;  // Rough kickoff detection
    }
    
    bool IsDodgingImminentDemo(const Player& player, 
                               const std::vector<Player>& players) {
        const float APPROACH_THRESHOLD = 1500.0f;
        const float TIME_THRESHOLD = 0.5f;
        
        for (const auto& opponent : players) {
            if (opponent.team == player.team) continue;
            
            Vec dirToUs = (player.pos - opponent.pos).Normalized();
            float velTowardUs = opponent.vel.Dot(dirToUs);
            
            if (velTowardUs > APPROACH_THRESHOLD) {
                float distToOpp = distance(opponent.pos, player.pos);
                float timeToContact = distToOpp / velTowardUs;
                
                if (timeToContact > 0 && timeToContact < TIME_THRESHOLD) {
                    return true;  // Opponent will demo us soon
                }
            }
        }
        
        return false;
    }
    
    bool InOpponentCorner(const Vec& pos) {
        const float CORNER_X = 3500.0f;
        const float CORNER_Y = 4000.0f;
        
        bool isCorner = (std::abs(pos.x) > CORNER_X) && (std::abs(pos.y) > CORNER_Y);
        bool isOppHalf = pos.y > 0;  // TODO: Make team-aware
        
        return isCorner && isOppHalf;
    }
    
    bool InOpponentGoalArea(const Vec& pos) {
        const float GOAL_WIDTH = 900.0f;
        const float GOAL_LINE_Y = 5120.0f;
        const float BACK_NET_Y = 6000.0f;
        const float GOAL_HEIGHT = 843.0f;
        
        bool inX = std::abs(pos.x) < GOAL_WIDTH;
        bool inY = pos.y > GOAL_LINE_Y && pos.y < BACK_NET_Y;
        bool inZ = pos.z < GOAL_HEIGHT + 200.0f;
        
        return inX && inY && inZ;
    }
    
    bool RecentlyAttemptedShot(const Player& player, float windowSeconds) {
        // Simple heuristic: was airborne recently and now low
        bool wasRecentlyAirborne = (player.hasFlip == false && !player.isOnGround);
        bool isNowLow = player.isOnGround && player.vel.z < 100.0f;
        
        // Better: track action history
        // TODO: Add to Player state tracking
        
        return wasRecentlyAirborne && isNowLow;
    }
    
    bool TeamHasGoalSideCoverage(const Player& player, 
                                 const std::vector<Player>& players) {
        // Check if a teammate is positioned to defend goal
        // Simple: is there a teammate in defensive half closer to goal than us?
        
        Vec ourGoal = Vec(0, -5120, 0);  // Blue goal (TODO: make team-aware)
        
        for (const auto& teammate : players) {
            if (teammate.team != player.team || teammate.carId == player.carId) {
                continue;
            }
            
            float theirDist = distance(teammate.pos, ourGoal);
            float ourDist = distance(player.pos, ourGoal);
            
            if (theirDist < ourDist && teammate.pos.y < player.pos.y) {
                return true;  // Teammate is goal-side
            }
        }
        
        return false;
    }
    
    float distance(const Vec& a, const Vec& b) const {
        return (a - b).Length();
    }
};

}  // namespace RLGC
