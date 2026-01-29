// =========================================================================
// HOOK SHOT REWARD - INTEGRATION GUIDE
// =========================================================================
//
// This file demonstrates how to integrate HookShotReward into your
// training pipeline (ExampleMain.cpp).
//
// The reward tracks three distinct phases:
//
// PHASE 1: ROLLING (0.3x reward)
// ├─ Ball is on ground with horizontal velocity 800-1600 uu/s
// ├─ Ball has minimal vertical velocity (< 100 uu/s)
// └─ Reward: Small continuous bonus for maintaining the roll
//
// PHASE 2: POSITIONING (0.5x reward)
// ├─ Car is at 30±15° angle from ball's travel direction
// ├─ Car is positioned 200-600 uu away from ball
// ├─ Car is at similar height to ball
// └─ Reward: Composite of angle accuracy + distance + height
//
// PHASE 3: EXECUTION (1.0x reward full / 0.5x rebounding)
// ├─ Car boosts and hits ball while positioned correctly
// ├─ Ball gains significant upward velocity (> 500 uu/s z-component)
// ├─ Ball is directed toward opponent goal (full reward)
// ├─ Ball rebounds off wall/backboard (half reward)
// └─ Metrics: On-goal success rate, rebound rate, average roll time
//
// =========================================================================
// STEP 1: INCLUDE THE REWARD
// =========================================================================
//
// Add to ExampleMain.cpp after other reward includes:
//
//     #include <RLGymCPP/Rewards/HookShotReward.h>
//
// =========================================================================
// STEP 2: ADD TO REWARD CONFIGURATION (BY PHASE)
// =========================================================================
//
// For PHASE 3 (GOLD) - Introduction:
//
//     case TrainingPhase::PHASE_3_GOLD:
//         return std::vector<WeightedReward>{
//             // ... existing rewards ...
//             WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 8.0f),
//             // ... rest of rewards ...
//         };
//
// For PHASE 4 (PLATINUM) - Development:
//
//     case TrainingPhase::PHASE_4_PLATINUM:
//         return std::vector<WeightedReward>{
//             // ... existing rewards ...
//             WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 12.0f),
//             // ... rest of rewards ...
//         };
//
// For PHASE 5 (DIAMOND) - Refinement:
//
//     case TrainingPhase::PHASE_5_DIAMOND:
//         return std::vector<WeightedReward>{
//             // ... existing rewards ...
//             WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 10.0f),
//             // ... rest of rewards ...
//         };
//
// For PHASE 6+ (CHAMPION/GC/SSL) - Polish:
//
//     case TrainingPhase::PHASE_6_CHAMPION:
//         return std::vector<WeightedReward>{
//             // ... existing rewards ...
//             WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 8.0f),
//             // ... rest of rewards ...
//         };
//
// =========================================================================
// STEP 3: ADD METRICS REPORTING IN StepCallback
// =========================================================================
//
// In the StepCallback function, add after other reward metrics:
//
//     {
//         auto m = RLGC::HookShotReward::GetAndResetHookShotMetrics();
//         if (m.attempts > 0) {
//             report.AddAvg("HookShot/Attempts", (float)m.attempts);
//             report.AddAvg("HookShot/OnGoalRate", m.GetSuccessRate());
//             report.AddAvg("HookShot/WallRebounds", (float)m.wallRebounds);
//             report.AddAvg("HookShot/AvgRollTime", m.GetAverageRollTime());
//             report.AddAvg("HookShot/TotalReward", m.totalRewardSum);
//         }
//     }
//
// This produces W&B metrics:
// - HookShot/Attempts: Number of hook shot attempts
// - HookShot/OnGoalRate: Success rate (0.0 to 1.0)
// - HookShot/WallRebounds: Number of wall rebounds per step
// - HookShot/AvgRollTime: Average roll duration in seconds
// - HookShot/TotalReward: Cumulative reward from phase rewards
//
// =========================================================================
// STEP 4: TUNING PARAMETERS
// =========================================================================
//
// If the bot struggles with any phase, adjust HookShotReward parameters:
//
// ROLLING PHASE (Ball setup):
// - minRollSpeed / maxRollSpeed: Adjust if ball speed is too high/low
//   Typical range: 800-1600 uu/s (50-100 kph)
// - maxRollZVel: If ball hops during roll, lower this (e.g., 50.0f)
// - maxRollHeight: If rolling at wrong height, adjust (e.g., 250.0f)
// - rollRewardScale: Increase if bot isn't maintaining rolls
//
// POSITIONING PHASE (Car angle/distance):
// - targetAngleDegrees: 30° is standard, adjust if needed (20-40°)
// - angleToleranceDegrees: ±15° tolerance, tighten to 10° for precision
// - positionRewardScale: Increase from 0.5f if bot struggles positioning
//
// EXECUTION PHASE (Shot mechanics):
// - minShotBoost: Lower (40.0f) if bot hits before accelerating
// - shotRewardScale: Increase to 1.5f for more aggressive training
// - reboundRewardScale: Increase to 0.7f if rebounds should be rewarded more
//
// =========================================================================
// STEP 5: EXPECTED BEHAVIOR BY PHASE
// =========================================================================
//
// PHASE 3-4 (Early training):
// - Bot learns to recognize rolling ball setup
// - Learns basic car positioning at angle
// - Success rate: 5-15%
// - Metrics: Attempts increasing, low on-goal rate
//
// PHASE 5 (Intermediate):
// - Bot executes full sequence occasionally
// - Positioning improves significantly
// - Success rate: 15-30%
// - Metrics: On-goal shots appearing, rebound rate stable
//
// PHASE 6+ (Advanced):
// - Bot consistently executes hook shots
// - Can chain multiple attempts
// - Success rate: 30-50%+
// - Metrics: High attempt rate, improving success rate
//
// =========================================================================
// STEP 6: DEBUGGING TIPS
// =========================================================================
//
// If W&B metrics show issues:
//
// 1. Attempts = 0?
//    - Bot not detecting rolling balls
//    - Reduce minRollSpeed to 600.0f, check maxRollHeight
//    - Verify ball physics in your environment
//
// 2. OnGoalRate very low (< 5%)?
//    - Positioning phase too strict
//    - Increase angleToleranceDegrees to 20-25°
//    - Lower minShotBoost to 40.0f
//
// 3. AvgRollTime very high (> 5 seconds)?
//    - Bot stalling on rolling phase
//    - Check positionRewardScale, increase to 0.7f
//    - Verify ball is actually rolling (not bouncing)
//
// 4. Wall rebounds too high?
//    - Shot direction is poor
//    - Lower shotRewardScale or increase goal-directed bonus
//    - Check angleToleranceDegrees alignment with goal
//
// =========================================================================
// EXAMPLE: COMPLETE PHASE 4 CONFIGURATION
// =========================================================================
//
// case TrainingPhase::PHASE_4_PLATINUM:
//     return std::vector<WeightedReward>{
//         // ... existing rewards ...
//         
//         // Hook shot - introduce with moderate weight
//         // Use ZeroSumReward to encourage outplay vs. traditional play
//         WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 12.0f),
//         
//         // ... rest of phase rewards ...
//     };
//
// With metrics reporting in StepCallback:
//
//     {
//         auto m = RLGC::HookShotReward::GetAndResetHookShotMetrics();
//         if (m.attempts > 0) {
//             report.AddAvg("HookShot/Attempts", (float)m.attempts);
//             report.AddAvg("HookShot/OnGoalRate", m.GetSuccessRate());
//             report.AddAvg("HookShot/WallRebounds", (float)m.wallRebounds);
//             report.AddAvg("HookShot/AvgRollTime", m.GetAverageRollTime());
//             report.AddAvg("HookShot/TotalReward", m.totalRewardSum);
//         }
//     }
//
// =========================================================================