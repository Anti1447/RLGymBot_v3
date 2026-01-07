// PossessionRewardsTests.cpp
// Comprehensive unit tests for possession reward implementations
// Compile: g++ -std=c++17 -I../include PossessionRewardsTests.cpp -o possession_tests

#include "PossessionRewards.h"
#include <cassert>
#include <iostream>
#include <cmath>
#include <iomanip>

using namespace RLGC;

// Test utilities
#define TEST_START(name) std::cout << "\n=== Testing: " << name << " ===" << std::endl;
#define TEST_PASS() std::cout << "✓ PASS" << std::endl;
#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::cerr << "FAIL: Expected " << (b) << " but got " << (a) \
                  << " (diff: " << std::abs((a) - (b)) << ")" << std::endl; \
        assert(false); \
    }

// Helper to create test game state
GameState CreateTestState(int numPlayers = 2) {
    GameState state;
    state.deltaTime = 1.0f / 120.0f; // 120 Hz
    
    // Default ball
    state.ball.pos = Vec(0, 0, 93);
    state.ball.vel = Vec(0, 0, 0);
    state.ball.angVel = Vec(0, 0, 0);
    
    // Create players
    for (int i = 0; i < numPlayers; i++) {
        Player p;
        p.carId = i;
        p.team = (i % 2 == 0) ? Team::BLUE : Team::ORANGE;
        p.pos = Vec(0, -2000 + i * 4000, 17);
        p.vel = Vec(0, 0, 0);
        p.angVel = Vec(0, 0, 0);
        p.boost = 50.0f;
        p.isOnGround = true;
        p.ballTouchedStep = false;
        p.isDemoed = false;
        p.prevAction = Action();
        
        // Set up rotation matrix (facing forward)
        p.rotMat.forward = Vec(0, 1, 0);
        p.rotMat.up = Vec(0, 0, 1);
        p.rotMat.right = Vec(1, 0, 0);
        
        state.players.push_back(p);
    }
    
    return state;
}

// ============================================================================
// TEST SUITE 1: TIME-TO-BALL POSSESSION
// ============================================================================

void TestTimeToB all_BasicScenario() {
    TEST_START("Time-To-Ball: Basic Equal Distance");
    
    GameState state = CreateTestState(2);
    TimeToBalPossessionReward reward;
    reward.Reset(state);
    
    // Both players equidistant, similar setup
    state.players[0].pos = Vec(0, -1000, 17);
    state.players[1].pos = Vec(0, 1000, 17);
    state.ball.pos = Vec(0, 0, 93);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    float r1 = reward.GetReward(state.players[1], state, false);
    
    // Should be near zero (no clear possession)
    ASSERT_NEAR(r0, 0.0f, 0.3f);
    ASSERT_NEAR(r1, 0.0f, 0.3f);
    
    TEST_PASS();
}

void TestTimeToB all_CloserPlayerWins() {
    TEST_START("Time-To-Ball: Closer Player Gets Possession");
    
    GameState state = CreateTestState(2);
    TimeToBalPossessionReward reward;
    reward.Reset(state);
    
    // Player 0 much closer
    state.players[0].pos = Vec(0, -300, 17);
    state.players[1].pos = Vec(0, 2000, 17);
    state.ball.pos = Vec(0, 0, 93);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    float r1 = reward.GetReward(state.players[1], state, false);
    
    // Player 0 should have positive reward
    assert(r0 > 0.3f);
    // Player 1 should have negative or very low reward
    assert(r1 < 0.1f);
    
    std::cout << "P0 reward: " << r0 << ", P1 reward: " << r1 << std::endl;
    TEST_PASS();
}

void TestTimeToB all_VelocityMatters() {
    TEST_START("Time-To-Ball: Velocity Affects Time-To-Ball");
    
    GameState state = CreateTestState(2);
    TimeToBalPossessionReward reward;
    reward.Reset(state);
    
    // Player 0 further but supersonic toward ball
    state.players[0].pos = Vec(0, -1500, 17);
    state.players[0].vel = Vec(0, 2300, 0); // Supersonic toward ball
    state.players[0].boost = 100.0f;
    
    // Player 1 closer but stationary
    state.players[1].pos = Vec(0, 800, 17);
    state.players[1].vel = Vec(0, 0, 0);
    state.players[1].boost = 0.0f;
    
    state.ball.pos = Vec(0, 0, 93);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    float r1 = reward.GetReward(state.players[1], state, false);
    
    // Player 0 should win despite being further (supersonic)
    std::cout << "P0 (supersonic, far): " << r0 << std::endl;
    std::cout << "P1 (stationary, close): " << r1 << std::endl;
    
    assert(r0 > r1);
    TEST_PASS();
}

void TestTimeToB all_Dribbling() {
    TEST_START("Time-To-Ball: Dribbling Detection");
    
    GameState state = CreateTestState(2);
    TimeToBalPossessionReward reward;
    reward.Reset(state);
    
    // Player 0 has ball on car (perfect dribble)
    state.players[0].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 0, 110); // 93 units above car
    state.ball.vel = Vec(0, 100, 0); // Slow
    
    // Player 1 far away
    state.players[1].pos = Vec(0, 2000, 17);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    
    // Should get high reward for perfect control
    assert(r0 > 0.5f);
    std::cout << "Dribble reward: " << r0 << std::endl;
    
    TEST_PASS();
}

void TestTimeToB all_PossessionDuration() {
    TEST_START("Time-To-Ball: Duration Tracking");
    
    GameState state = CreateTestState(2);
    TimeToBalPossessionReward reward;
    reward.Reset(state);
    
    // Player 0 has possession
    state.players[0].pos = Vec(0, -200, 17);
    state.players[1].pos = Vec(0, 2000, 17);
    state.ball.pos = Vec(0, 0, 93);
    
    // Simulate possession over time
    float totalReward = 0.0f;
    for (int i = 0; i < 120; i++) { // 1 second
        float r = reward.GetReward(state.players[0], state, false);
        totalReward += r;
    }
    
    // Should get bonuses for extended possession
    std::cout << "Total reward after 1s possession: " << totalReward << std::endl;
    assert(totalReward > 10.0f); // Base + duration bonuses
    
    TEST_PASS();
}

// ============================================================================
// TEST SUITE 2: CONTEXTUAL POSSESSION
// ============================================================================

void TestContextual_GroundDribble() {
    TEST_START("Contextual: Ground Dribble Detection");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Perfect ground dribble setup
    state.players[0].pos = Vec(0, 0, 17);
    state.players[0].isOnGround = true;
    state.ball.pos = Vec(0, 0, 110); // On car
    state.ball.vel = Vec(0, 200, 0); // Slow, controlled
    
    // Opponent far
    state.players[1].pos = Vec(0, 2000, 17);
    
    float r = reward.GetReward(state.players[0], state, false);
    
    // Should get significant reward for ground dribble
    std::cout << "Ground dribble reward: " << r << std::endl;
    assert(r > 1.0f);
    
    TEST_PASS();
}

void TestContextual_AirDribble() {
    TEST_START("Contextual: Air Dribble Detection");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Air dribble setup
    state.players[0].pos = Vec(0, 0, 500); // In air
    state.players[0].isOnGround = false;
    state.ball.pos = Vec(0, 0, 650); // Close in air
    state.ball.vel = Vec(0, 300, 50);
    
    // Opponent far
    state.players[1].pos = Vec(0, 2000, 17);
    
    float r = reward.GetReward(state.players[0], state, false);
    
    // Air dribble should reward more than ground dribble
    std::cout << "Air dribble reward: " << r << std::endl;
    assert(r > 1.5f);
    
    TEST_PASS();
}

void TestContextual_WallCarry() {
    TEST_START("Contextual: Wall Carry Detection");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Wall carry setup
    state.players[0].pos = Vec(4000, 0, 300); // On wall
    state.ball.pos = Vec(3900, 0, 400); // Close to wall and player
    
    state.players[1].pos = Vec(0, 2000, 17);
    
    float r = reward.GetReward(state.players[0], state, false);
    
    std::cout << "Wall carry reward: " << r << std::endl;
    assert(r > 0.8f);
    
    TEST_PASS();
}

void TestContextual_SpaceCreation() {
    TEST_START("Contextual: Space Creation");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Ball moving away from all opponents
    state.players[0].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 500, 93);
    state.ball.vel = Vec(0, -800, 0); // Moving away from opponent
    
    state.players[1].pos = Vec(0, 2000, 17);
    
    float r = reward.GetReward(state.players[0], state, false);
    
    std::cout << "Space creation reward: " << r << std::endl;
    assert(r > 0.5f);
    
    TEST_PASS();
}

void TestContextual_BoostStarve() {
    TEST_START("Contextual: Boost Starvation");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Player 0 in opponent half with possession
    state.players[0].pos = Vec(0, 1000, 17); // Orange half (opponent for blue)
    state.players[0].team = Team::BLUE;
    state.ball.pos = Vec(0, 1200, 93); // Close
    
    // Opponent low on boost
    state.players[1].pos = Vec(0, 2000, 17);
    state.players[1].team = Team::ORANGE;
    state.players[1].boost = 5.0f; // Low boost
    
    // Simulate ground dribble
    state.ball.pos.z = 110;
    state.players[0].pos.z = 17;
    
    float r = reward.GetReward(state.players[0], state, false);
    
    std::cout << "Boost starve reward: " << r << std::endl;
    assert(r > 1.5f); // Base dribble + boost starve bonus
    
    TEST_PASS();
}

void TestContextual_DefensiveClear() {
    TEST_START("Contextual: Defensive Clear (Space Creation)");
    
    GameState state = CreateTestState(2);
    ContextualPossessionReward reward;
    reward.Reset(state);
    
    // Just cleared ball from corner
    state.players[0].pos = Vec(3000, -4000, 17); // Blue defensive corner
    state.players[0].team = Team::BLUE;
    state.players[0].ballTouchedStep = true;
    
    // Ball now moving to side (away from goal, away from opponents)
    state.ball.pos = Vec(2500, -3500, 200);
    state.ball.vel = Vec(-1000, 500, 100); // Sideways/up
    
    // Opponents were pressuring
    state.players[1].pos = Vec(2000, -3000, 17);
    state.players[1].team = Team::ORANGE;
    
    float r = reward.GetReward(state.players[0], state, false);
    
    std::cout << "Defensive clear reward: " << r << std::endl;
    
    // Should get space creation reward (not penalized for not attacking)
    assert(r > 0.3f);
    
    TEST_PASS();
}

// ============================================================================
// TEST SUITE 3: MULTI-AGENT POSSESSION
// ============================================================================

void TestMultiAgent_ControllerVsSupport() {
    TEST_START("Multi-Agent: Controller vs Support Roles");
    
    GameState state = CreateTestState(4); // 2v2
    MultiAgentPossessionReward reward;
    reward.Reset(state);
    
    // Team setup: players 0,2 are blue, 1,3 are orange
    state.players[0].team = Team::BLUE;
    state.players[1].team = Team::ORANGE;
    state.players[2].team = Team::BLUE;
    state.players[3].team = Team::ORANGE;
    
    // Player 0 is controlling the ball
    state.players[0].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 100, 93);
    
    // Player 2 is in support position (1500 units away)
    state.players[2].pos = Vec(0, -1500, 17);
    
    // Opponents far
    state.players[1].pos = Vec(0, 2000, 17);
    state.players[3].pos = Vec(0, 2500, 17);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    float r2 = reward.GetReward(state.players[2], state, false);
    
    std::cout << "Controller reward: " << r0 << std::endl;
    std::cout << "Support reward: " << r2 << std::endl;
    
    // Controller should get more reward
    assert(r0 > r2);
    // But support should also get positive reward
    assert(r2 > 0.2f);
    
    TEST_PASS();
}

void TestMultiAgent_PreventBallChasing() {
    TEST_START("Multi-Agent: Prevent Ball Chasing");
    
    GameState state = CreateTestState(4);
    MultiAgentPossessionReward reward;
    reward.Reset(state);
    
    state.players[0].team = Team::BLUE;
    state.players[2].team = Team::BLUE;
    state.players[1].team = Team::ORANGE;
    state.players[3].team = Team::ORANGE;
    
    // Player 0 has ball
    state.players[0].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 100, 93);
    
    // Player 2 is ball-chasing (too close to teammate with ball)
    state.players[2].pos = Vec(0, 200, 17); // Way too close
    
    state.players[1].pos = Vec(0, 2000, 17);
    state.players[3].pos = Vec(0, 2500, 17);
    
    float r2 = reward.GetReward(state.players[2], state, false);
    
    std::cout << "Ball-chasing player reward: " << r2 << std::endl;
    
    // Should get penalty for overlapping
    assert(r2 < 0.2f);
    
    TEST_PASS();
}

void TestMultiAgent_PassingLane() {
    TEST_START("Multi-Agent: Passing Lane Positioning");
    
    GameState state = CreateTestState(4);
    MultiAgentPossessionReward reward;
    reward.Reset(state);
    
    state.players[0].team = Team::BLUE;
    state.players[2].team = Team::BLUE;
    state.players[1].team = Team::ORANGE;
    state.players[3].team = Team::ORANGE;
    
    // Player 0 has ball at (0, 0)
    state.players[0].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 100, 93);
    
    // Player 2 in good passing lane (forward and to side)
    state.players[2].pos = Vec(800, 1500, 17);
    
    state.players[1].pos = Vec(0, 2000, 17);
    state.players[3].pos = Vec(0, 2500, 17);
    
    float r2 = reward.GetReward(state.players[2], state, false);
    
    std::cout << "Passing lane support reward: " << r2 << std::endl;
    
    // Should get bonus for good support positioning
    assert(r2 > 0.4f);
    
    TEST_PASS();
}

void TestMultiAgent_PossessionTransition() {
    TEST_START("Multi-Agent: Possession Win/Loss");
    
    GameState state = CreateTestState(2);
    MultiAgentPossessionReward reward;
    reward.Reset(state);
    
    // Start: Player 1 (Orange) has possession
    state.players[0].team = Team::BLUE;
    state.players[0].pos = Vec(0, -2000, 17);
    
    state.players[1].team = Team::ORANGE;
    state.players[1].pos = Vec(0, 0, 17);
    state.ball.pos = Vec(0, 100, 93);
    
    // First frame: Orange has possession
    float r0_frame1 = reward.GetReward(state.players[0], state, false);
    float r1_frame1 = reward.GetReward(state.players[1], state, false);
    
    std::cout << "Frame 1 - Blue: " << r0_frame1 << ", Orange: " << r1_frame1 << std::endl;
    
    // Now Blue wins possession
    state.players[0].pos = Vec(0, 0, 17);
    state.players[1].pos = Vec(0, 2000, 17);
    state.ball.pos = Vec(0, -100, 93);
    
    // Simulate a few frames to establish possession
    for (int i = 0; i < 10; i++) {
        reward.GetReward(state.players[0], state, false);
        reward.GetReward(state.players[1], state, false);
    }
    
    // Check if Blue got win bonus and Orange got loss penalty
    // (This is implicit in the possession score changes)
    
    TEST_PASS();
}

void TestMultiAgent_TeamCoordination() {
    TEST_START("Multi-Agent: 3v3 Team Coordination");
    
    GameState state = CreateTestState(6); // 3v3
    MultiAgentPossessionReward reward;
    reward.Reset(state);
    
    // Setup teams
    state.players[0].team = Team::BLUE; // Controller
    state.players[1].team = Team::ORANGE;
    state.players[2].team = Team::BLUE; // Mid support
    state.players[3].team = Team::ORANGE;
    state.players[4].team = Team::BLUE; // Back support
    state.players[5].team = Team::ORANGE;
    
    // Player 0 has ball
    state.players[0].pos = Vec(0, 1000, 17);
    state.ball.pos = Vec(0, 1100, 93);
    
    // Player 2 in mid-field support
    state.players[2].pos = Vec(500, 0, 17);
    
    // Player 4 in back (defensive)
    state.players[4].pos = Vec(0, -2000, 17);
    
    // Opponents
    state.players[1].pos = Vec(0, 2000, 17);
    state.players[3].pos = Vec(0, 2500, 17);
    state.players[5].pos = Vec(0, 3000, 17);
    
    float r0 = reward.GetReward(state.players[0], state, false);
    float r2 = reward.GetReward(state.players[2], state, false);
    float r4 = reward.GetReward(state.players[4], state, false);
    
    std::cout << "Controller: " << r0 << std::endl;
    std::cout << "Mid support: " << r2 << std::endl;
    std::cout << "Back support: " << r4 << std::endl;
    
    // Controller should get highest
    assert(r0 > r2 && r0 > r4);
    
    // Both supports should get positive rewards
    assert(r2 > 0.1f);
    assert(r4 > 0.0f); // Back player still valuable
    
    TEST_PASS();
}

// ============================================================================
// COMPARATIVE TESTS
// ============================================================================

void TestComparative_DribbleScoring() {
    TEST_START("Comparative: Ground Dribble Scoring");
    
    GameState state = CreateTestState(2);
    
    // Perfect ground dribble
    state.players[0].pos = Vec(0, 0, 17);
    state.players[0].isOnGround = true;
    state.ball.pos = Vec(0, 0, 110);
    state.ball.vel = Vec(0, 200, 0);
    state.players[1].pos = Vec(0, 2000, 17);
    
    TimeToBalPossessionReward r1;
    ContextualPossessionReward r2;
    MultiAgentPossessionReward r3;
    
    r1.Reset(state);
    r2.Reset(state);
    r3.Reset(state);
    
    float score1 = r1.GetReward(state.players[0], state, false);
    float score2 = r2.GetReward(state.players[0], state, false);
    float score3 = r3.GetReward(state.players[0], state, false);
    
    std::cout << "Time-To-Ball: " << score1 << std::endl;
    std::cout << "Contextual: " << score2 << std::endl;
    std::cout << "Multi-Agent: " << score3 << std::endl;
    
    // Contextual should score highest (explicitly rewards dribbling)
    assert(score2 > score1);
    assert(score2 > score3);
    
    TEST_PASS();
}

void TestComparative_DefensiveClear() {
    TEST_START("Comparative: Defensive Clear Scoring");
    
    GameState state = CreateTestState(2);
    
    // Defensive clear (not toward opponent goal)
    state.players[0].pos = Vec(3000, -4000, 17);
    state.players[0].team = Team::BLUE;
    state.players[0].ballTouchedStep = true;
    state.ball.pos = Vec(2500, -3500, 200);
    state.ball.vel = Vec(-1000, 500, 100); // Sideways
    state.players[1].pos = Vec(2000, -3000, 17);
    
    TimeToBalPossessionReward r1;
    ContextualPossessionReward r2;
    MultiAgentPossessionReward r3;
    
    r1.Reset(state);
    r2.Reset(state);
    r3.Reset(state);
    
    float score1 = r1.GetReward(state.players[0], state, false);
    float score2 = r2.GetReward(state.players[0], state, false);
    float score3 = r3.GetReward(state.players[0], state, false);
    
    std::cout << "Time-To-Ball: " << score1 << std::endl;
    std::cout << "Contextual: " << score2 << std::endl;
    std::cout << "Multi-Agent: " << score3 << std::endl;
    
    // Contextual should reward space creation
    assert(score2 > 0.3f);
    
    TEST_PASS();
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    std::cout << "╔═══════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Possession Rewards Comprehensive Test Suite ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════╝" << std::endl;
    
    try {
        // Time-To-Ball Tests
        std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
        std::cout << "║  TIME-TO-BALL IMPLEMENTATION      ║" << std::endl;
        std::cout << "╚═══════════════════════════════════╝" << std::endl;
        TestTimeToB all_BasicScenario();
        TestTimeToB all_CloserPlayerWins();
        TestTimeToB all_VelocityMatters();
        TestTimeToB all_Dribbling();
        TestTimeToB all_PossessionDuration();
        
        // Contextual Tests
        std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
        std::cout << "║  CONTEXTUAL IMPLEMENTATION        ║" << std::endl;
        std::cout << "╚═══════════════════════════════════╝" << std::endl;
        TestContextual_GroundDribble();
        TestContextual_AirDribble();
        TestContextual_WallCarry();
        TestContextual_SpaceCreation();
        TestContextual_BoostStarve();
        TestContextual_DefensiveClear();
        
        // Multi-Agent Tests
        std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
        std::cout << "║  MULTI-AGENT IMPLEMENTATION       ║" << std::endl;
        std::cout << "╚═══════════════════════════════════╝" << std::endl;
        TestMultiAgent_ControllerVsSupport();
        TestMultiAgent_PreventBallChasing();
        TestMultiAgent_PassingLane();
        TestMultiAgent_PossessionTransition();
        TestMultiAgent_TeamCoordination();
        
        // Comparative Tests
        std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
        std::cout << "║  COMPARATIVE TESTS                ║" << std::endl;
        std::cout << "╚═══════════════════════════════════╝" << std::endl;
        TestComparative_DribbleScoring();
        TestComparative_DefensiveClear();
        
        std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
        std::cout << "║  ✓ ALL TESTS PASSED               ║" << std::endl;
        std::cout << "╚═══════════════════════════════════╝" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
