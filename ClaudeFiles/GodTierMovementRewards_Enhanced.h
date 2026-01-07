// GodTierMovementRewards_Enhanced.h
// Enhanced with detailed mechanic understanding from video breakdown
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>

namespace RLGC {

// ============================================================================
// ENHANCED SPEED FLIP REWARD
// "Flip forward slightly to the side, then drag stick down to bottom"
// ============================================================================
// 2025-12-09 | Rev 1.1 | AI
// - Added explicit partial rewards for:
//   - diagonal flip start
//   - cancel during flip
//   - forward alignment before/after flip
// - Kept graded “perfect speed flip” bonus on top.


class EnhancedSpeedFlipReward : public Reward {
private:
    struct FlipData {
        Vec initialForward;
        Vec initialVelocity;
        Vec flipDirection;
        float startSpeed;
        float startTime;
        bool cancelDetected;
        bool diagonalFlipDetected;
        bool airRollUsed;
    };
    
    mutable std::map<uint32_t, FlipData> flipData;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable float currentTime;

public:
    virtual void Reset(const GameState& initialState) override {
        flipData.clear();
        isFlipping.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;
        
        // ===== FLIP START DETECTION =====
        if (!wasFlipping && player.isFlipping) {
            FlipData data;
            data.initialForward = player.rotMat.forward;
            data.initialVelocity = player.vel;
            data.startSpeed = player.vel.Length();
            data.startTime = currentTime;
            data.cancelDetected = false;
            data.diagonalFlipDetected = false;
            data.airRollUsed = false;
            
            // Detect diagonal flip (slightly to the side)
            // Speed flip should NOT be straight forward
            Vec velDir = player.vel.Normalized();
            Vec forwardDir = player.rotMat.forward;
            float forwardAlignment = velDir.Dot(forwardDir);
            
            // Check for diagonal component (pitch + yaw input)
            float pitch = player.prevAction.pitch;
            float yaw = player.prevAction.yaw;
            
            // Speed flip = forward flip with slight side component
            if (pitch > 0.5f && abs(yaw) > 0.2f && abs(yaw) < 0.8f) {
                data.diagonalFlipDetected = true;
                data.flipDirection = Vec(yaw, pitch, 0).Normalized();
            }
            
            flipData[player.carId] = data;
            return 0.0f;
        }
        
        // ===== DURING FLIP - DETECT CANCEL =====
        if (player.isFlipping && wasFlipping) {
            auto& data = flipData[player.carId];
            
            // Check for air roll usage
            if (abs(player.prevAction.roll) > 0.3f || 
                abs(player.prevAction.yaw) > 0.3f) {
                data.airRollUsed = true;
            }
            
            // Detect cancel: "drag stick down to bottom"
            // This means pitch goes from positive (forward) to negative (backward)
            float pitch = player.prevAction.pitch;
            
            if (pitch < -0.7f) { // Strong backward input = cancel
                data.cancelDetected = true;
            }
        }
        
        // ===== FLIP END - EVALUATE SPEED FLIP =====
        if (wasFlipping && !player.isFlipping) {
            const FlipData& data = flipData[player.carId];

            float endSpeed = player.vel.Length();
            float speedGain = endSpeed - data.startSpeed;
            float flipDuration = currentTime - data.startTime;

            // --- Forward alignment (start vs end) ---
            Vec startForward = data.initialForward.Normalized();
            Vec endForward = player.rotMat.forward.Normalized();
            float forwardAlign = startForward.Dot(endForward);   // 1.0 = same, 0 = orthogonal

            // === PARTIAL REWARDS ===

            // 1) Diagonal flip start (small reward)
            if (data.diagonalFlipDetected) {
                reward += 0.5f;
            }

            // 2) Flip cancel during diagonal flip (medium reward)
            if (data.cancelDetected && data.diagonalFlipDetected) {
                reward += 1.0f;
            }

            // 3) Start forward and end forward (medium reward)
            if (forwardAlign > 0.85f) { // ~30° cone
                reward += 1.0f;
            }

            // === FULL SPEED FLIP CRITERIA (bonus on top) ===
            bool criteriaA = data.diagonalFlipDetected;
            bool criteriaB = data.cancelDetected;
            bool criteriaC = data.airRollUsed;
            bool criteriaD = speedGain > 300.0f;
            bool criteriaE = flipDuration < 1.0f;
            bool criteriaF = endSpeed > 1800.0f;

            int criteriaMet =
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0) +
                (criteriaE ? 1 : 0) +
                (criteriaF ? 1 : 0);

            if (criteriaMet >= 5) {
                // Perfect / near-perfect speed flip
                reward += 3.0f;   // keep this modest; partials already paid

                if (criteriaMet == 6) {
                    reward += 2.0f;   // textbook speed flip
                }
            }
            else if (criteriaMet >= 3) {
                // Decent attempt: scale a small bonus
                reward += 1.0f * (criteriaMet / 6.0f);
            }
            else {
                // Failed attempt: they tried but technique was poor
                if (data.diagonalFlipDetected || data.cancelDetected) {
                    reward -= 0.5f;
                }
            }
        }
        return reward;
    }
};

// ============================================================================
// ENHANCED MENA FLIP REWARD
// "Turn wheels AWAY from wall, speed flip in direction wheels face"
// ============================================================================

class EnhancedMenaFlipReward : public Reward {
private:
    struct MenaData {
        Vec preFlipPosition;
        Vec preFlipVelocity;
        Vec wheelDirection;
        float preFlipSpeed;
        float startTime;
        bool wheelsAwayFromWall;
        bool speedFlipPerformed;
        bool correctAirRollUsed;
        Side wallSide;
    };
    
    enum class Side { NONE, LEFT, RIGHT };
    
    mutable std::map<uint32_t, MenaData> menaData;
    mutable std::map<uint32_t, bool> isFlipping;
    mutable float currentTime;
    
    Side DetectWallSide(const Vec& pos) const {
        float distToRightWall = abs(pos.x - CommonValues::SIDE_WALL_X);
        float distToLeftWall = abs(pos.x + CommonValues::SIDE_WALL_X);
        
        if (distToRightWall < 800.0f) return Side::RIGHT;
        if (distToLeftWall < 800.0f) return Side::LEFT;
        return Side::NONE;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        menaData.clear();
        isFlipping.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        bool wasFlipping = isFlipping[player.carId];
        isFlipping[player.carId] = player.isFlipping;
        
        Side wallSide = DetectWallSide(player.pos);
        
        // ===== APPROACHING WALL =====
        if (wallSide != Side::NONE && player.isOnGround) {
            auto& data = menaData[player.carId];
            data.preFlipPosition = player.pos;
            data.preFlipVelocity = player.vel;
            data.preFlipSpeed = player.vel.Length();
            data.wallSide = wallSide;
            
            // Check if wheels turned AWAY from wall
            // Right wall: should turn left (yaw < 0)
            // Left wall: should turn right (yaw > 0)
            float yaw = player.prevAction.yaw;
            
            if (wallSide == Side::RIGHT && yaw < -0.3f) {
                data.wheelsAwayFromWall = true;
            } else if (wallSide == Side::LEFT && yaw > 0.3f) {
                data.wheelsAwayFromWall = true;
            } else {
                data.wheelsAwayFromWall = false;
            }
        }
        
        // ===== FLIP START =====
        if (!wasFlipping && player.isFlipping && wallSide != Side::NONE) {
            auto& data = menaData[player.carId];
            data.startTime = currentTime;
            
            // Check for diagonal flip (speed flip)
            float pitch = player.prevAction.pitch;
            float yaw = player.prevAction.yaw;
            
            if (pitch > 0.5f && abs(yaw) > 0.2f) {
                data.speedFlipPerformed = true;
            }
        }
        
        // ===== DURING FLIP =====
        if (player.isFlipping && wasFlipping) {
            auto& data = menaData[player.carId];
            
            // Check for correct air roll
            // Right wall: air roll right (roll > 0)
            // Left wall: air roll left (roll < 0)
            float roll = player.prevAction.roll;
            
            if (data.wallSide == Side::RIGHT && roll > 0.5f) {
                data.correctAirRollUsed = true;
            } else if (data.wallSide == Side::LEFT && roll < -0.5f) {
                data.correctAirRollUsed = true;
            }
        }
        
        // ===== AFTER FLIP - ON WALL =====
        if (wasFlipping && !player.isFlipping) {
            const MenaData& data = menaData[player.carId];
            
            // Check if now on wall
            float distToWallX = RS_MIN(
                abs(player.pos.x - CommonValues::SIDE_WALL_X),
                abs(player.pos.x + CommonValues::SIDE_WALL_X)
            );
            bool onWall = distToWallX < 150.0f && player.pos.z > 100.0f;
            
            float endSpeed = player.vel.Length();
            float speedGain = endSpeed - data.preFlipSpeed;
            
            // === MENA FLIP CRITERIA (from video) ===
            // 1. Wheels turned away from wall
            // 2. Speed flip performed
            // 3. Correct air roll direction used
            // 4. Ended up on wall
            // 5. Speed gained (>200)
            
            bool criteriaA = data.wheelsAwayFromWall;
            bool criteriaB = data.speedFlipPerformed;
            bool criteriaC = data.correctAirRollUsed;
            bool criteriaD = onWall;
            bool criteriaE = speedGain > 200.0f;
            
            int criteriaMet = 
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0) +
                (criteriaE ? 1 : 0);
            
            if (criteriaMet >= 4) {
                // SUCCESSFUL MENA FLIP
                reward += 12.0f;
                
                // Perfect technique bonus
                if (criteriaMet == 5) {
                    reward += 6.0f; // Textbook Mena flip!
                }
                
                // Speed gain scaling
                reward += RS_MIN((speedGain - 200.0f) / 100.0f, 5.0f);
                
                // Supersonic bonus
                if (endSpeed > 2200.0f) {
                    reward += 4.0f;
                }
                
            } else if (criteriaB || criteriaC) {
                // Attempted but failed
                reward -= 2.0f; // Teach proper technique
            }
        }
        
        return reward;
    }
};

// ============================================================================
// ENHANCED WALL DASH REWARD
// "Jump twice as fast as you can in direction of wall"
// ============================================================================

class EnhancedWallDashReward : public Reward {
private:
    struct WallDashData {
        float firstJumpTime;
        float secondJumpTime;
        Vec positionAtFirstJump;
        float speedAtFirstJump;
        Side wallSide;
        bool correctDirection;
    };
    
    enum class Side { NONE, LEFT, RIGHT, FRONT, BACK };
    
    mutable std::map<uint32_t, WallDashData> wallDashData;
    mutable std::map<uint32_t, int> jumpCount;
    mutable float currentTime;
    
    Side DetectWallSide(const Vec& pos) const {
        float distToRightWall = abs(pos.x - CommonValues::SIDE_WALL_X);
        float distToLeftWall = abs(pos.x + CommonValues::SIDE_WALL_X);
        float distToFrontWall = abs(pos.y - CommonValues::BACK_WALL_Y);
        float distToBackWall = abs(pos.y + CommonValues::BACK_WALL_Y);
        
        float minDist = RS_MIN(RS_MIN(distToRightWall, distToLeftWall),
                              RS_MIN(distToFrontWall, distToBackWall));
        
        if (minDist > 300.0f) return Side::NONE;
        
        if (minDist == distToRightWall) return Side::RIGHT;
        if (minDist == distToLeftWall) return Side::LEFT;
        if (minDist == distToFrontWall) return Side::FRONT;
        return Side::BACK;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        wallDashData.clear();
        jumpCount.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        Side wallSide = DetectWallSide(player.pos);
        
        // Not on wall - reset
        if (wallSide == Side::NONE || player.pos.z < 50.0f) {
            jumpCount[player.carId] = 0;
            return 0.0f;
        }
        
        // ===== DETECT JUMPS ON WALL =====
        bool jumped = false;
        if (state.prev && !state.prev->players[player.index].isOnGround && player.isOnGround) {
            // Just landed on wall - doesn't count as jump
        } else if (state.prev) {
            // Detect jump by upward velocity spike
            float prevZVel = state.prev->players[player.index].vel.z;
            float curZVel = player.vel.z;
            
            if (curZVel > prevZVel + 150.0f) {
                jumped = true;
            }
        }
        
        if (!jumped) return 0.0f;
        
        // ===== FIRST JUMP =====
        if (jumpCount[player.carId] == 0) {
            WallDashData data;
            data.firstJumpTime = currentTime;
            data.positionAtFirstJump = player.pos;
            data.speedAtFirstJump = player.vel.Length();
            data.wallSide = wallSide;
            
            // Check jump direction relative to wall
            // Should be jumping TOWARD the wall's tangent direction
            float yaw = player.prevAction.yaw;
            
            // Right wall: should jump right (yaw > 0)
            // Left wall: should jump left (yaw < 0)
            if ((wallSide == Side::RIGHT && yaw > 0.3f) ||
                (wallSide == Side::LEFT && yaw < -0.3f) ||
                (wallSide == Side::FRONT && player.prevAction.pitch > 0.3f) ||
                (wallSide == Side::BACK && player.prevAction.pitch < -0.3f)) {
                data.correctDirection = true;
            } else {
                data.correctDirection = false;
            }
            
            wallDashData[player.carId] = data;
            jumpCount[player.carId] = 1;
            
        // ===== SECOND JUMP =====
        } else if (jumpCount[player.carId] == 1) {
            const WallDashData& data = wallDashData[player.carId];
            float timeBetweenJumps = currentTime - data.firstJumpTime;
            
            // === WALL DASH CRITERIA (from video) ===
            // 1. Two jumps EXTREMELY FAST (<0.25s)
            // 2. In direction of wall
            // 3. On wall throughout
            // 4. Speed gained
            
            bool criteriaA = timeBetweenJumps < 0.25f; // "as fast as you can"
            bool criteriaB = data.correctDirection;
            bool criteriaC = wallSide == data.wallSide;
            
            float endSpeed = player.vel.Length();
            float speedGain = endSpeed - data.speedAtFirstJump;
            bool criteriaD = speedGain > 80.0f;
            
            if (criteriaA && criteriaB && criteriaC && criteriaD) {
                // SUCCESSFUL WALL DASH
                reward += 8.0f;
                
                // Speed bonus for VERY fast execution
                if (timeBetweenJumps < 0.15f) {
                    reward += 4.0f; // Lightning fast!
                }
                
                // Speed gain scaling
                reward += RS_MIN(speedGain / 80.0f, 4.0f);
                
                // No boost used = perfect
                if (state.prev) {
                    float prevBoost = state.prev->players[player.index].boost;
                    float boostUsed = prevBoost - player.boost;
                    if (boostUsed == 0.0f) {
                        reward += 3.0f; // Pure wall dash!
                    }
                }
                
            } else if (criteriaA) {
                // Fast jumps but poor execution
                reward -= 1.0f;
            }
            
            jumpCount[player.carId] = 0;
        }
        
        return reward;
    }
};

// ============================================================================
// ENHANCED ZAP DASH REWARD
// "Land front wheels, immediately jump when front wheels planted"
// ============================================================================

class EnhancedZapDashReward : public Reward {
private:
    struct ZapDashData {
        float landingTime;
        float landingSpeed;
        Vec landingVelocity;
        bool frontWheelsFirst;
        float frontWheelPitch;
    };
    
    mutable std::map<uint32_t, ZapDashData> zapDashData;
    mutable std::map<uint32_t, bool> wasAirborne;
    mutable float currentTime;

public:
    virtual void Reset(const GameState& initialState) override {
        zapDashData.clear();
        wasAirborne.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;
        
        bool wasInAir = wasAirborne[player.carId];
        bool inAir = !player.isOnGround;
        wasAirborne[player.carId] = inAir;
        
        // ===== LANDING MOMENT =====
        if (wasInAir && !inAir) {
            ZapDashData data;
            data.landingTime = currentTime;
            data.landingSpeed = player.vel.Length();
            data.landingVelocity = player.vel;
            
            // Check if front wheels landed first
            // This is indicated by nose-down pitch
            Vec forward = player.rotMat.forward;
            data.frontWheelPitch = forward.z; // Negative = nose down
            
            if (data.frontWheelPitch < -0.4f) {
                data.frontWheelsFirst = true;
            } else {
                data.frontWheelsFirst = false;
            }
            
            zapDashData[player.carId] = data;
        }
        
        // ===== JUMP AFTER LANDING =====
        if (!wasInAir && inAir) {
            const ZapDashData& data = zapDashData[player.carId];
            float timeSinceLanding = currentTime - data.landingTime;
            
            // === ZAP DASH CRITERIA (from video) ===
            // 1. Land on front wheels (nose down)
            // 2. Jump IMMEDIATELY when front wheels planted (<0.15s)
            // 3. NO flip used (distinguishes from wave dash)
            // 4. Speed gained
            
            bool criteriaA = data.frontWheelsFirst;
            bool criteriaB = timeSinceLanding < 0.15f; // Very quick!
            bool criteriaC = !player.isFlipping;
            
            float currentSpeed = player.vel.Length();
            float speedGain = currentSpeed - data.landingSpeed;
            bool criteriaD = speedGain > 50.0f;
            
            int criteriaMet = 
                (criteriaA ? 1 : 0) +
                (criteriaB ? 1 : 0) +
                (criteriaC ? 1 : 0) +
                (criteriaD ? 1 : 0);
            
            if (criteriaMet >= 3) {
                // SUCCESSFUL ZAP DASH
                reward += 6.0f;
                
                // Perfect technique bonus
                if (criteriaMet == 4) {
                    reward += 3.0f;
                }
                
                // Extra bonus for very nose-down landing
                if (data.frontWheelPitch < -0.6f) {
                    reward += 2.0f; // Perfect front wheel absorption
                }
                
                // Speed gain scaling
                reward += RS_MIN(speedGain / 50.0f, 3.0f);
                
                // Timing bonus
                if (timeSinceLanding < 0.08f) {
                    reward += 2.0f; // Lightning reflexes!
                }
                
                // Supersonic maintenance
                if (currentSpeed > 2200.0f) {
                    reward += 2.0f;
                }
                
            } else if (criteriaB && !criteriaA) {
                // Quick jump but wrong technique
                reward -= 0.5f; // Teach proper front-wheel landing
            }
        }
        
        return reward;
    }
};

// ============================================================================
// TRAINING DIFFICULTY SETTINGS
// ============================================================================

struct MovementTrainingConfig {
    // Speed flip requirements
    float speedFlipMinSpeed = 1800.0f;      // Easy: 1600, Hard: 2000
    float speedFlipMinGain = 300.0f;        // Easy: 200,  Hard: 400
    int speedFlipMinCriteria = 5;           // Easy: 3,    Hard: 6
    
    // Mena flip requirements
    float menaFlipMinSpeed = 1700.0f;       // Easy: 1500, Hard: 1900
    int menaFlipMinCriteria = 4;            // Easy: 3,    Hard: 5
    
    // Wall dash requirements
    float wallDashMaxTime = 0.25f;          // Easy: 0.35, Hard: 0.15
    float wallDashMinSpeed = 80.0f;         // Easy: 50,   Hard: 120
    
    // Zap dash requirements
    float zapDashMaxTime = 0.15f;           // Easy: 0.25, Hard: 0.10
    float zapDashMinPitch = -0.4f;          // Easy: -0.3, Hard: -0.5
    
    static MovementTrainingConfig Easy() {
        MovementTrainingConfig cfg;
        cfg.speedFlipMinSpeed = 1600.0f;
        cfg.speedFlipMinGain = 200.0f;
        cfg.speedFlipMinCriteria = 3;
        cfg.menaFlipMinSpeed = 1500.0f;
        cfg.menaFlipMinCriteria = 3;
        cfg.wallDashMaxTime = 0.35f;
        cfg.wallDashMinSpeed = 50.0f;
        cfg.zapDashMaxTime = 0.25f;
        cfg.zapDashMinPitch = -0.3f;
        return cfg;
    }
    
    static MovementTrainingConfig Hard() {
        MovementTrainingConfig cfg;
        cfg.speedFlipMinSpeed = 2000.0f;
        cfg.speedFlipMinGain = 400.0f;
        cfg.speedFlipMinCriteria = 6;
        cfg.menaFlipMinSpeed = 1900.0f;
        cfg.menaFlipMinCriteria = 5;
        cfg.wallDashMaxTime = 0.15f;
        cfg.wallDashMinSpeed = 120.0f;
        cfg.zapDashMaxTime = 0.10f;
        cfg.zapDashMinPitch = -0.5f;
        return cfg;
    }
};

} // namespace RLGC
