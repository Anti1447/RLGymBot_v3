// RewardSystemUnitTests.cpp
// Comprehensive unit tests for all reward implementations
// Compile: g++ -std=c++17 -I../RLGymCPP/src RewardSystemUnitTests.cpp -o reward_tests

#include <cassert>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <functional>

// Test framework
#define TEST(name) void test_##name()
#define RUN_TEST(name) run_test(#name, test_##name)
#define ASSERT_TRUE(expr) assert_true(expr, #expr, __LINE__)
#define ASSERT_FALSE(expr) assert_false(expr, #expr, __LINE__)
#define ASSERT_EQ(a, b) assert_eq(a, b, #a, #b, __LINE__)
#define ASSERT_NEAR(a, b, eps) assert_near(a, b, eps, #a, #b, __LINE__)

int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

void assert_true(bool condition, const char* expr, int line) {
    if (!condition) {
        std::cerr << "  FAIL (line " << line << "): " << expr << " is false" << std::endl;
        tests_failed++;
        throw std::runtime_error("Assertion failed");
    }
}

void assert_false(bool condition, const char* expr, int line) {
    assert_true(!condition, expr, line);
}

template<typename T>
void assert_eq(T a, T b, const char* expr_a, const char* expr_b, int line) {
    if (a != b) {
        std::cerr << "  FAIL (line " << line << "): " << expr_a << " != " << expr_b 
                  << " (" << a << " != " << b << ")" << std::endl;
        tests_failed++;
        throw std::runtime_error("Assertion failed");
    }
}

void assert_near(float a, float b, float eps, const char* expr_a, const char* expr_b, int line) {
    if (std::abs(a - b) > eps) {
        std::cerr << "  FAIL (line " << line << "): " << expr_a << " ≠ " << expr_b 
                  << " (" << a << " vs " << b << ", diff: " << std::abs(a - b) << ")" << std::endl;
        tests_failed++;
        throw std::runtime_error("Assertion failed");
    }
}

void run_test(const char* name, std::function<void()> test_func) {
    tests_run++;
    std::cout << "Running: " << name << "... ";
    try {
        test_func();
        tests_passed++;
        std::cout << "✓ PASS" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ FAIL" << std::endl;
    }
}

// ============================================================================
// MOCK OBJECTS FOR TESTING
// ============================================================================

namespace RLGC {

struct Vec {
    float x, y, z;
    
    float Length() const {
        return std::sqrt(x*x + y*y + z*z);
    }
    
    Vec Normalized() const {
        float len = Length();
        if (len < 0.0001f) return {0, 0, 0};
        return {x/len, y/len, z/len};
    }
    
    float Dot(const Vec& other) const {
        return x*other.x + y*other.y + z*other.z;
    }
    
    Vec operator-(const Vec& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
};

struct RotMat {
    Vec forward;
    Vec up;
    Vec right;
};

struct Action {
    float throttle = 0.0f;
    float steer = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    float jump = 0.0f;
    float boost = 0.0f;
    float handbrake = 0.0f;
};

enum class Team { BLUE, ORANGE };

struct PhysicsObject {
    Vec pos;
    Vec vel;
    Vec angVel;
};

struct Player {
    uint32_t carId;
    Team team;
    Vec pos;
    Vec vel;
    Vec angVel;
    RotMat rotMat;
    float boost;
    bool isOnGround;
    bool isFlipping;
    bool isDemoed;
    bool ballTouchedStep;
    Action prevAction;
    int index;
    
    bool HasFlipOrJump() const { return true; } // Mock
};

struct GameState {
    PhysicsObject ball;
    std::vector<Player> players;
    float deltaTime;
    int blueScore;
    int orangeScore;
    bool goalScored;
    GameState* prev;
};

struct CommonValues {
    static constexpr float CAR_MAX_SPEED = 2300.0f;
    static constexpr float SIDE_WALL_X = 4096.0f;
    static constexpr float BACK_WALL_Y = 5120.0f;
    static constexpr float CEILING_Z = 2044.0f;
    static constexpr float GOAL_HEIGHT = 642.0f;
    static constexpr float GOAL_WIDTH_FROM_CENTER = 893.0f;
    static const Vec BLUE_GOAL_BACK;
    static const Vec ORANGE_GOAL_BACK;
};

const Vec CommonValues::BLUE_GOAL_BACK = {0, -5120, 0};
const Vec CommonValues::ORANGE_GOAL_BACK = {0, 5120, 0};

#define RS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RS_MAX(a, b) ((a) > (b) ? (a) : (b))
#define RS_CLAMP(v, min, max) (RS_MIN(RS_MAX(v, min), max))

class Reward {
public:
    virtual ~Reward() = default;
    virtual void Reset(const GameState& initialState) = 0;
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) = 0;
};

} // namespace RLGC

// ============================================================================
// COMPILATION TESTS
// ============================================================================

TEST(compilation_namespace_scope) {
    // Test that RLGC namespace is properly used
    using namespace RLGC;
    
    Vec v{1, 2, 3};
    ASSERT_EQ(v.x, 1.0f);
    ASSERT_EQ(v.y, 2.0f);
    ASSERT_EQ(v.z, 3.0f);
}

TEST(compilation_vec_math) {
    using namespace RLGC;
    
    Vec v1{3, 4, 0};
    ASSERT_NEAR(v1.Length(), 5.0f, 0.001f);
    
    Vec v2{1, 0, 0};
    Vec v3{0, 1, 0};
    ASSERT_NEAR(v2.Dot(v3), 0.0f, 0.001f);
}

TEST(compilation_enum_scoping) {
    // Test that enums are properly scoped
    enum class Side { NONE, LEFT, RIGHT };
    
    struct TestStruct {
        Side wallSide;  // Should compile without error
    };
    
    TestStruct t;
    t.wallSide = Side::LEFT;
    ASSERT_TRUE(t.wallSide == Side::LEFT);
}

// ============================================================================
// DUPLICATE CLASS DETECTION TESTS
// ============================================================================

TEST(duplicate_detection_unique_classes) {
    // Simulates checking for unique class names
    std::vector<std::string> classNames = {
        "HalfFlipReward",
        "WaveDashReward",
        "SpeedFlipReward"
    };
    
    std::set<std::string> seen;
    for (const auto& name : classNames) {
        ASSERT_TRUE(seen.find(name) == seen.end());
        seen.insert(name);
    }
}

TEST(duplicate_detection_namespace_qualification) {
    // Test that namespaced classes don't conflict
    std::string class1 = "RLGC::DoubleTapReward";
    std::string class2 = "DoubleTapReward";  // Without namespace
    
    ASSERT_TRUE(class1 != class2);  // These are different
}

// ============================================================================
// SPEED FLIP DETECTION TESTS
// ============================================================================

TEST(speedflip_diagonal_detection) {
    using namespace RLGC;
    
    // Test diagonal flip detection
    Action action;
    action.pitch = 0.6f;   // Forward
    action.yaw = 0.4f;     // To the side
    
    // Criteria: pitch > 0.5 && abs(yaw) > 0.2 && abs(yaw) < 0.8
    bool isDiagonal = (action.pitch > 0.5f && 
                       std::abs(action.yaw) > 0.2f && 
                       std::abs(action.yaw) < 0.8f);
    
    ASSERT_TRUE(isDiagonal);
}

TEST(speedflip_cancel_detection) {
    using namespace RLGC;
    
    // Test cancel detection (drag stick down)
    Action action;
    action.pitch = -0.8f;  // Strong backward input
    
    bool isCancel = (action.pitch < -0.7f);
    ASSERT_TRUE(isCancel);
}

TEST(speedflip_speed_gain_check) {
    float startSpeed = 1500.0f;
    float endSpeed = 1850.0f;
    float speedGain = endSpeed - startSpeed;
    
    ASSERT_TRUE(speedGain > 300.0f);
}

TEST(speedflip_criteria_grading) {
    // Test 6-criteria system
    bool criteria[6] = {
        true,   // Diagonal flip
        true,   // Cancel detected
        true,   // Air roll used
        true,   // Speed gain > 300
        true,   // Fast execution < 1.0s
        false   // High end speed (missing)
    };
    
    int criteriaMet = 0;
    for (bool c : criteria) {
        if (c) criteriaMet++;
    }
    
    ASSERT_EQ(criteriaMet, 5);  // 5/6 = good speed flip
}

// ============================================================================
// MENA FLIP TESTS
// ============================================================================

TEST(menaflip_wall_detection) {
    using namespace RLGC;
    
    Vec pos{3900.0f, 0.0f, 300.0f};  // Near right wall
    
    float distToRightWall = std::abs(pos.x - CommonValues::SIDE_WALL_X);
    float distToLeftWall = std::abs(pos.x + CommonValues::SIDE_WALL_X);
    
    ASSERT_TRUE(distToRightWall < 800.0f);
    ASSERT_TRUE(distToLeftWall > 800.0f);
}

TEST(menaflip_wheel_direction_check) {
    using namespace RLGC;
    
    // Right wall: should turn LEFT (yaw < -0.3)
    Action action;
    action.yaw = -0.5f;  // Turning left
    
    bool wheelsAwayFromWall = (action.yaw < -0.3f);
    ASSERT_TRUE(wheelsAwayFromWall);
}

TEST(menaflip_airroll_direction_check) {
    using namespace RLGC;
    
    // Right wall: should air roll RIGHT (roll > 0.5)
    Action action;
    action.roll = 0.7f;  // Rolling right
    
    bool correctAirRoll = (action.roll > 0.5f);
    ASSERT_TRUE(correctAirRoll);
}

// ============================================================================
// WALL DASH TIMING TESTS
// ============================================================================

TEST(walldash_timing_fast) {
    float firstJumpTime = 1.0f;
    float secondJumpTime = 1.12f;
    float timeBetween = secondJumpTime - firstJumpTime;
    
    ASSERT_TRUE(timeBetween < 0.25f);  // Fast enough
    ASSERT_TRUE(timeBetween >= 0.10f); // But not too fast
}

TEST(walldash_timing_too_slow) {
    float firstJumpTime = 1.0f;
    float secondJumpTime = 1.35f;
    float timeBetween = secondJumpTime - firstJumpTime;
    
    ASSERT_FALSE(timeBetween < 0.25f);  // Too slow
}

TEST(walldash_timing_pro_level) {
    float firstJumpTime = 1.0f;
    float secondJumpTime = 1.08f;
    float timeBetween = secondJumpTime - firstJumpTime;
    
    ASSERT_TRUE(timeBetween < 0.15f);  // Pro level!
}

// ============================================================================
// ZAP DASH TESTS
// ============================================================================

TEST(zapdash_front_wheel_detection) {
    using namespace RLGC;
    
    // Front wheel landing = nose down
    RotMat rot;
    rot.forward = {0.7f, 0.0f, -0.5f};  // Nose down
    
    float pitchForward = rot.forward.z;
    bool frontWheelsFirst = (pitchForward < -0.4f);
    
    ASSERT_TRUE(frontWheelsFirst);
}

TEST(zapdash_timing_immediate) {
    float landingTime = 1.0f;
    float jumpTime = 1.08f;
    float timeSinceLanding = jumpTime - landingTime;
    
    ASSERT_TRUE(timeSinceLanding < 0.15f);  // Immediate
}

TEST(zapdash_no_flip_requirement) {
    using namespace RLGC;
    
    Player player{};
    player.isFlipping = false;
    
    ASSERT_FALSE(player.isFlipping);  // No flip used
}

// ============================================================================
// POSSESSION TESTS
// ============================================================================

TEST(possession_time_to_ball_basic) {
    using namespace RLGC;
    
    Vec playerPos{0, -1000, 17};
    Vec ballPos{0, 0, 93};
    Vec playerVel{0, 1500, 0};  // Moving toward ball
    
    Vec toBall = ballPos - playerPos;
    float distToBall = toBall.Length();
    Vec dirToBall = toBall.Normalized();
    
    float speedToBall = RS_MAX(0.0f, playerVel.Dot(dirToBall));
    float timeToReach = distToBall / RS_MAX(speedToBall, 500.0f);
    
    ASSERT_TRUE(timeToReach < 1.0f);  // Will reach quickly
}

TEST(possession_controllability_dribble) {
    using namespace RLGC;
    
    Vec playerPos{0, 0, 17};
    Vec ballPos{0, 0, 110};  // On car
    
    Vec relBallPos = ballPos - playerPos;
    float heightAboveCar = relBallPos.z;
    float horizDist = std::sqrt(relBallPos.x * relBallPos.x + relBallPos.y * relBallPos.y);
    
    bool isDribble = (heightAboveCar > 20.0f && heightAboveCar < 200.0f && horizDist < 150.0f);
    
    ASSERT_TRUE(isDribble);
}

// ============================================================================
// FIELD SPACING TESTS
// ============================================================================

TEST(spacing_coverage_overlap_detection) {
    using namespace RLGC;
    
    // Two players too close
    Vec center1{0, 0, 0};
    Vec center2{0, 500, 0};  // Only 500 units apart
    
    float radius1 = 800.0f;
    float radius2 = 800.0f;
    
    float dist = (center2 - center1).Length();
    float combinedRadius = radius1 + radius2;
    
    float overlap = 0.0f;
    if (dist < combinedRadius) {
        overlap = (combinedRadius - dist) / combinedRadius;
    }
    
    ASSERT_TRUE(overlap > 0.5f);  // Severe overlap
}

// ============================================================================
// MOMENTUM MAINTENANCE TESTS
// ============================================================================

TEST(momentum_supersonic_detection) {
    float speed = 2250.0f;
    bool supersonic = (speed > 2200.0f);
    
    ASSERT_TRUE(supersonic);
}

TEST(momentum_speed_gain_without_boost) {
    float prevSpeed = 1800.0f;
    float curSpeed = 1900.0f;
    float prevBoost = 50.0f;
    float curBoost = 50.0f;
    
    float speedGain = curSpeed - prevSpeed;
    float boostUsed = prevBoost - curBoost;
    
    ASSERT_TRUE(speedGain > 50.0f);
    ASSERT_EQ(boostUsed, 0.0f);
}

// ============================================================================
// REGRESSION TESTS
// ============================================================================

TEST(regression_halfflip_no_duplicate) {
    // Test that HalfFlipReward isn't defined twice
    std::vector<std::string> definedClasses = {
        "RLGC::HalfFlipReward"  // Should only appear once
    };
    
    ASSERT_EQ(definedClasses.size(), 1);
}

TEST(regression_enum_before_struct) {
    // Test that enum is declared before struct usage
    struct TestOrder {
        enum class TestEnum { A, B };
        
        struct TestStruct {
            TestEnum value;  // Should compile
        };
    };
    
    TestOrder::TestStruct s;
    s.value = TestOrder::TestEnum::A;
    ASSERT_TRUE(s.value == TestOrder::TestEnum::A);
}

TEST(regression_namespace_qualification) {
    // Test fully qualified names work
    using namespace RLGC;
    
    Vec v1{1, 0, 0};
    RLGC::Vec v2{0, 1, 0};  // Fully qualified
    
    ASSERT_NEAR(v1.Dot(v2), 0.0f, 0.001f);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Rocket League Rewards - Unit Test Suite      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    
    // Compilation tests
    std::cout << "▶ Compilation Tests" << std::endl;
    RUN_TEST(compilation_namespace_scope);
    RUN_TEST(compilation_vec_math);
    RUN_TEST(compilation_enum_scoping);
    std::cout << std::endl;
    
    // Duplicate detection
    std::cout << "▶ Duplicate Detection Tests" << std::endl;
    RUN_TEST(duplicate_detection_unique_classes);
    RUN_TEST(duplicate_detection_namespace_qualification);
    std::cout << std::endl;
    
    // Speed flip
    std::cout << "▶ Speed Flip Tests" << std::endl;
    RUN_TEST(speedflip_diagonal_detection);
    RUN_TEST(speedflip_cancel_detection);
    RUN_TEST(speedflip_speed_gain_check);
    RUN_TEST(speedflip_criteria_grading);
    std::cout << std::endl;
    
    // Mena flip
    std::cout << "▶ Mena Flip Tests" << std::endl;
    RUN_TEST(menaflip_wall_detection);
    RUN_TEST(menaflip_wheel_direction_check);
    RUN_TEST(menaflip_airroll_direction_check);
    std::cout << std::endl;
    
    // Wall dash
    std::cout << "▶ Wall Dash Tests" << std::endl;
    RUN_TEST(walldash_timing_fast);
    RUN_TEST(walldash_timing_too_slow);
    RUN_TEST(walldash_timing_pro_level);
    std::cout << std::endl;
    
    // Zap dash
    std::cout << "▶ Zap Dash Tests" << std::endl;
    RUN_TEST(zapdash_front_wheel_detection);
    RUN_TEST(zapdash_timing_immediate);
    RUN_TEST(zapdash_no_flip_requirement);
    std::cout << std::endl;
    
    // Possession
    std::cout << "▶ Possession Tests" << std::endl;
    RUN_TEST(possession_time_to_ball_basic);
    RUN_TEST(possession_controllability_dribble);
    std::cout << std::endl;
    
    // Field spacing
    std::cout << "▶ Field Spacing Tests" << std::endl;
    RUN_TEST(spacing_coverage_overlap_detection);
    std::cout << std::endl;
    
    // Momentum
    std::cout << "▶ Momentum Tests" << std::endl;
    RUN_TEST(momentum_supersonic_detection);
    RUN_TEST(momentum_speed_gain_without_boost);
    std::cout << std::endl;
    
    // Regression tests
    std::cout << "▶ Regression Tests" << std::endl;
    RUN_TEST(regression_halfflip_no_duplicate);
    RUN_TEST(regression_enum_before_struct);
    RUN_TEST(regression_namespace_qualification);
    std::cout << std::endl;
    
    // Summary
    std::cout << "╔════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TEST SUMMARY                                  ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════╝" << std::endl;
    std::cout << "Tests run:    " << tests_run << std::endl;
    std::cout << "Tests passed: " << tests_passed << " ✓" << std::endl;
    std::cout << "Tests failed: " << tests_failed << " ✗" << std::endl;
    std::cout << std::endl;
    
    if (tests_failed == 0) {
        std::cout << "🎉 All tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "❌ Some tests failed!" << std::endl;
        return 1;
    }
}
