# Automated Reward Engineering System for RLGymBot v3

## Overview

This document describes a complete closed-loop system that automates your four core responsibilities:
1. Running training
2. Coding new rewards
3. Debugging reward failures (compilation, behavioral issues)
4. Balancing reward weights

The system uses a multi-tier feedback loop: Training Metrics → Heuristics Analysis → LLM-Driven Code Generation → Compilation/Runtime Validation → GA-Based Weight Optimization.

---

## Architecture Components

### Tier 1: Training Orchestration & Metrics Collection

**Purpose**: Automatically run training, collect detailed metrics, and identify performance bottlenecks.

```cpp
// RewardMetricsCollector.h
struct RewardMetrics {
    std::string rewardName;
    float activationRate;           // % of steps where reward > 0
    float meanActivationValue;      // Average reward when active
    float varianceInActivation;     // Stability of reward signal
    float correlationWithWinRate;   // Does this help the bot win?
    float correlationWithOtherRewards[50];  // Conflict detection
    int compilationFailures;
    int runtimeErrors;
    float averageLatency;
    
    // Derived metrics
    float signalQuality;            // High activation + low variance + high win correlation
    float gradientConflictScore;    // How much does it conflict with others?
    std::vector<std::string> conflictingRewards;
};

class TrainingMetricsCollector {
public:
    // During training, collect per-reward data every N steps
    void CollectMetrics(
        const std::vector<WeightedReward>& rewards,
        const GameState& state,
        float episodeReward,
        bool episodeWon
    );
    
    // After training phase, generate comprehensive report
    RewardReport GeneratePhaseReport(int stepCount);
    
    // Identify underperforming rewards
    std::vector<RewardMetrics> GetUnderperformingRewards(float threshold = 0.3f);
    
    // Detect conflicting reward pairs
    std::vector<RewardConflict> DetectRewardConflicts();
};
```

---

## Tier 2: Heuristic Analysis Engine

**Purpose**: Translate raw metrics into actionable reward engineering directives.

```cpp
// RewardAnalysisEngine.h

enum class RewardProblemType {
    LOW_ACTIVATION,              // Reward never fires
    HIGH_VARIANCE,               // Reward is noisy
    POOR_CORRELATION,            // Doesn't improve win rate
    CONFLICTING_WITH_OTHER,      // Works against other rewards
    INCORRECT_SCALING,           // Right idea, wrong weight
    REWARD_SATURATION,           // Doesn't scale with performance
    COMPILE_TIME_ISSUE,          // Doesn't compile
    RUNTIME_CRASH,               // Crashes during execution
    GRADIENT_DILUTION            // Lost among 40+ rewards
};

struct RewardDiagnosis {
    std::string rewardName;
    RewardProblemType problemType;
    float severity;              // 0.0 to 1.0
    std::string explanation;     // Human-readable description
    std::vector<std::string> suggestedFixes;  // What an AI should try
    
    // For LLM generation
    std::string metricsContext;  // Formatted metrics for context window
    std::string existingCode;    // Current implementation
    std::vector<RewardMetrics> conflictingRewards;  // What's fighting this
};

class RewardDiagnostician {
public:
    std::vector<RewardDiagnosis> DiagnoseRewards(
        const std::vector<RewardMetrics>& metrics,
        const std::map<std::string, std::string>& rewardImplementations
    );
    
    RewardDiagnosis DiagnoseSingleReward(
        const RewardMetrics& metrics,
        float baseWeight,
        const std::vector<RewardMetrics>& allMetrics
    );
    
    // Specific detection functions
    bool IsActivationTooLow(const RewardMetrics& m, float threshold = 0.05f);
    bool IsVarianceTooHigh(const RewardMetrics& m, float threshold = 0.7f);
    bool IsConflictingWithOthers(
        const RewardMetrics& m,
        const std::vector<RewardMetrics>& all,
        float conflictThreshold = 0.5f
    );
};
```

**Heuristic Rules for Problem Detection**:

```
Rule 1 - LOW_ACTIVATION:
  IF activationRate < 5% AND rewardWeight < 0.5f
    → Problem: Not occurring enough to learn from
    → Fix: Increase sensitivity thresholds OR increase weight
  
Rule 2 - HIGH_VARIANCE:
  IF varianceInActivation > 0.8 AND meanActivationValue < 0.5f
    → Problem: Noisy gradient signal
    → Fix: Smooth the reward function OR add state-dependent scaling
  
Rule 3 - GRADIENT_CONFLICT:
  IF correlationWithOtherRewards[X] < -0.3 AND both weights > 5.0f
    → Problem: Reward X and Y are optimizing opposing behaviors
    → Fix: Restructure both rewards with proper state gating
  
Rule 4 - POOR_WIN_CORRELATION:
  IF correlationWithWinRate < 0.2 AND activationRate > 0.3f
    → Problem: Reward fires but doesn't help winning
    → Fix: Retarget what behavior you're rewarding
  
Rule 5 - REWARD_SATURATION:
  IF meanActivationValue plateaus despite increasing weight
    → Problem: Reached terminal performance on this reward
    → Fix: Add complexity/difficulty scaling
```

---

## Tier 3: LLM-Driven Code Generation

**Purpose**: Generate new reward code or modifications based on diagnoses.

### 3.1: Structured Prompt Generation

```python
# reward_prompt_generator.py

class RewardPromptGenerator:
    def generate_fix_prompt(
        self,
        diagnosis: RewardDiagnosis,
        metrics: RewardMetrics,
        existing_code: str,
        similar_working_rewards: List[str]  # Examples of good patterns
    ) -> str:
        """
        Generate a structured prompt for LLM to fix a broken reward
        """
        prompt = f"""
You are a Rocket League AI training expert. Your task is to fix a reward function.

DIAGNOSIS:
Problem Type: {diagnosis.problemType}
Severity: {diagnosis.severity}
Explanation: {diagnosis.explanation}

METRICS:
- Activation Rate: {metrics.activationRate}%
- Mean Value: {metrics.meanActivationValue}
- Variance: {metrics.varianceInActivation}
- Win Correlation: {metrics.correlationWithWinRate}
- Conflicts: {', '.join(diagnosis.conflictingRewards)}

CURRENT CODE:
```cpp
{existing_code}
```

SIMILAR WORKING REWARDS (as reference):
{self._format_examples(similar_working_rewards)}

CONSTRAINTS:
1. Must compile with C++17 and LibTorch
2. Must inherit from Reward or RewardWrapper
3. Must implement: float Evaluate(const GameState&, int player, const TerminalCondition&)
4. Avoid expensive computation in evaluate()
5. Return values typically 0.0 to 1.0 (can exceed for importance)
6. Account for 8-tick simulation delay

REQUIREMENTS:
- Fix the identified problem: {diagnosis.problemType}
- Maintain backward compatibility where possible
- Add comments explaining the changes
- Consider interactions with these conflicting rewards: {', '.join(diagnosis.conflictingRewards)}

Generate ONLY the fixed C++ code, no explanation.
"""
        return prompt

    def generate_new_reward_prompt(
        self,
        desired_behavior: str,
        similar_rewards: List[str],
        phase: str
    ) -> str:
        """
        Generate a prompt to create an entirely new reward
        """
        prompt = f"""
You are a Rocket League AI reward engineer. Generate a new reward function.

DESIRED BEHAVIOR:
{desired_behavior}

PHASE CONTEXT:
Current Phase: {phase}
Expected Bot Skill Level: Diamond-level mechanics

SIMILAR EXISTING REWARDS (for reference):
{self._format_examples(similar_rewards)}

GUIDELINES:
1. The reward should be specific, measurable, and implementable
2. Consider what game state features are available (positions, velocities, rotations)
3. Avoid rewards that duplicate existing ones
4. Design for stability (smooth gradients, bounded outputs)
5. Account for multi-agent interactions (3v3 gameplay)
6. Consider temporal dynamics (reward should make sense over multiple ticks)

TECHNICAL CONSTRAINTS:
- C++17 with LibTorch
- Must compile within the RLGymCPP framework
- Performance-critical: should execute in <1ms per agent
- Return float (0.0-1.0 typical range)

Generate ONLY the C++ class implementation, no explanation.
"""
        return prompt
```

### 3.2: LLM Integration

```python
# llm_reward_generator.py

class LLMRewardGenerator:
    def __init__(self, api_key: str, model: str = "claude-opus-4-5"):
        self.client = anthropic.Anthropic(api_key=api_key)
        self.model = model
        self.compilation_history = {}  # Track what's compiled before
    
    async def fix_reward(
        self,
        diagnosis: RewardDiagnosis,
        metrics: RewardMetrics,
        existing_code: str,
        similar_working_rewards: List[str]
    ) -> Tuple[str, bool]:
        """
        Ask LLM to fix a broken reward
        Returns: (generated_code, successfully_compiles)
        """
        prompt = RewardPromptGenerator.generate_fix_prompt(
            diagnosis, metrics, existing_code, similar_working_rewards
        )
        
        # Generate code with Claude
        message = self.client.messages.create(
            model=self.model,
            max_tokens=2000,
            messages=[
                {"role": "user", "content": prompt}
            ]
        )
        
        generated_code = message.content[0].text
        
        # Clean up if wrapped in markdown
        generated_code = self._extract_cpp_code(generated_code)
        
        # Try to compile
        compiled_ok = await self._test_compile(generated_code, diagnosis.rewardName)
        
        return generated_code, compiled_ok
    
    async def create_new_reward(
        self,
        desired_behavior: str,
        phase: str,
        similar_rewards: List[str]
    ) -> Tuple[str, str, bool]:
        """
        Generate an entirely new reward function
        Returns: (reward_name, code, successfully_compiles)
        """
        prompt = RewardPromptGenerator.generate_new_reward_prompt(
            desired_behavior, similar_rewards, phase
        )
        
        message = self.client.messages.create(
            model=self.model,
            max_tokens=3000,
            messages=[
                {"role": "user", "content": prompt}
            ]
        )
        
        generated_code = self._extract_cpp_code(message.content[0].text)
        reward_name = self._extract_class_name(generated_code)
        
        compiled_ok = await self._test_compile(generated_code, reward_name)
        
        return reward_name, generated_code, compiled_ok
    
    async def _test_compile(self, code: str, reward_name: str) -> bool:
        """
        Test if code compiles in actual project
        Uses cmake/ninja to validate
        """
        # Write temp file
        temp_path = f"/tmp/test_{reward_name}.h"
        with open(temp_path, "w") as f:
            f.write(code)
        
        # Try to compile with actual project setup
        result = subprocess.run(
            ["cmake", "--build", "build", "--target", "compile_test"],
            capture_output=True,
            timeout=30
        )
        
        return result.returncode == 0
    
    def _extract_cpp_code(self, text: str) -> str:
        """Remove markdown formatting if present"""
        if "```cpp" in text:
            start = text.find("```cpp") + 6
            end = text.find("```", start)
            return text[start:end].strip()
        elif "```" in text:
            start = text.find("```") + 3
            end = text.find("```", start)
            return text[start:end].strip()
        return text.strip()
```

---

## Tier 4: Compilation & Runtime Validation

**Purpose**: Automatically test generated code before deployment.

```cpp
// RewardValidator.h

struct ValidationResult {
    bool compilesSuccessfully;
    bool runsWithoutCrash;
    bool producesValidValues;
    bool performsBetter;
    
    std::string compileError;
    std::string runtimeError;
    std::vector<float> sampleOutputs;
    float deltaWinRate;
};

class RewardValidator {
public:
    // Test 1: Compilation
    bool ValidateCompilation(const std::string& rewardCode);
    
    // Test 2: Runtime safety
    ValidationResult ValidateRuntime(
        const Reward* reward,
        int testEpisodes = 100
    );
    
    // Test 3: Behavioral correctness
    bool ValidateBehavior(
        const Reward* newReward,
        const Reward* oldReward,
        const std::vector<GameState>& testCases
    );
    
private:
    // Crash detection wrapper
    float EvaluateWithSafetyCheck(
        const Reward* reward,
        const GameState& state,
        int player
    );
};

// Integration with training loop
class AdaptiveRewardTrainer {
    void TrainingLoop() {
        while (shouldTrain) {
            // 1. Run training for N steps
            RunTraining(STEPS_PER_EVALUATION);
            
            // 2. Collect metrics
            auto metrics = metricsCollector.GeneratePhaseReport(totalSteps);
            
            // 3. Diagnose problems
            auto diagnoses = diagnostician.DiagnoseRewards(metrics, rewardCode);
            
            // 4. For each problem, consider fixing it
            for (const auto& diagnosis : diagnoses) {
                if (diagnosis.severity > 0.6f) {  // Only fix serious problems
                    
                    // Generate fix
                    auto [code, compiles] = llmGenerator.FixReward(
                        diagnosis, 
                        metrics[diagnosis.rewardName],
                        rewardCode[diagnosis.rewardName],
                        GetSimilarWorkingRewards()
                    );
                    
                    if (compiles) {
                        // Validate it actually helps
                        auto validationResult = validator.ValidateRuntime(
                            CreateRewardFromCode(code)
                        );
                        
                        if (validationResult.performsBetter) {
                            // Swap it in
                            rewardCode[diagnosis.rewardName] = code;
                            LogRewardChange(diagnosis.rewardName, "automated_fix");
                        }
                    }
                }
            }
            
            // 5. Optimize weights with GA (existing approach)
            auto optimizedWeights = geneticOptimizer.OptimizeWeights(metrics);
            ApplyWeights(optimizedWeights);
            
            // 6. Log everything for later analysis
            LogTrainingState(metrics, diagnoses);
        }
    }
};
```

---

## Tier 5: New Reward Suggestion Engine

**Purpose**: Proactively suggest new rewards based on performance gaps.

```python
# reward_suggestion_engine.py

class RewardSuggestionEngine:
    """
    Analyze training metrics and suggest new rewards
    """
    
    def __init__(self, training_history: Dict):
        self.history = training_history
    
    def identify_skill_gaps(self) -> List[str]:
        """
        Find mechanics the bot is weak at but hasn't been explicitly trained on
        """
        gaps = []
        
        # Compare win rate in different game scenarios
        scenarios = {
            "ball_in_air": self.history.get("win_rate_aerial_situations", 0.0),
            "close_to_goal": self.history.get("win_rate_scoring_distance", 0.0),
            "defender_positioning": self.history.get("win_rate_defensive", 0.0),
            "boost_management": self.history.get("average_boost_efficiency", 0.0),
        }
        
        for scenario, win_rate in scenarios.items():
            if win_rate < 0.4:  # Below 40% success
                gaps.append(scenario)
        
        return gaps
    
    def suggest_new_rewards(self, skill_gaps: List[str]) -> List[str]:
        """
        For each skill gap, suggest what rewards to add
        """
        suggestions = []
        
        for gap in skill_gaps:
            if gap == "ball_in_air":
                suggestions.append("""
                A reward that encourages the bot to read and intercept high balls
                better. Currently it misses easy aerial touches when the ball is
                already elevated. Consider a 'high_ball_prediction' reward that
                rewards positioning the car under the predicted ball landing spot.
                """)
            
            elif gap == "close_to_goal":
                suggestions.append("""
                A reward for accurate finishing. Bot gets close to the goal but
                shoots inaccurately. Consider a 'shot_accuracy' reward that measures
                angle and velocity relative to goal.
                """)
            
            # ... more gap types
        
        return suggestions
    
    def generate_reward_implementation_prompt(self, gap: str) -> str:
        """
        Create a detailed prompt for the LLM to implement the reward
        """
        return f"""
Based on analysis of 51 billion training steps, the bot has a skill gap in:
{gap}

This shows up as: [specific metric evidence]

Design a reward function that addresses this gap. Consider:
1. How is success measured in this scenario?
2. What game state features indicate this skill?
3. How should the reward scale with performance?
4. What's the risk of negative side effects on other skills?

Implement as a C++ Reward subclass.
"""
```

---

## Tier 6: Feedback Loop Integration

**Purpose**: Connect all tiers into one continuous cycle.

```python
# automated_training_orchestrator.py

class AutomatedTrainingOrchestrator:
    """
    Main controller that runs the entire closed-loop system
    """
    
    def __init__(self, config: TrainingConfig):
        self.config = config
        self.metrics_collector = TrainingMetricsCollector()
        self.diagnostician = RewardDiagnostician()
        self.llm_generator = LLMRewardGenerator(api_key=os.getenv("ANTHROPIC_API_KEY"))
        self.validator = RewardValidator()
        self.suggestion_engine = RewardSuggestionEngine()
        self.ga_optimizer = GeneticAlgorithmOptimizer()
        
        self.log_file = open("training_automation.log", "w")
    
    async def run_training_phase(self, phase: TrainingPhase):
        """
        Main training loop for a phase
        """
        self.log(f"Starting automated training for {phase}")
        
        iteration = 0
        max_iterations = 50  # Safety limit per phase
        
        while iteration < max_iterations:
            # Step 1: Run training
            self.log(f"Iteration {iteration}: Running training for {self.config.steps_per_eval} steps")
            self.run_training_subprocess(self.config.steps_per_eval)
            
            # Step 2: Collect metrics
            self.log("Collecting metrics...")
            metrics = self.metrics_collector.GeneratePhaseReport()
            self.log(f"  - Collected metrics for {len(metrics)} rewards")
            
            # Step 3: Diagnose problems
            self.log("Diagnosing reward problems...")
            diagnoses = self.diagnostician.DiagnoseRewards(
                metrics,
                self.load_reward_implementations()
            )
            
            # Step 4: Fix problems with LLM
            self.log(f"Found {len(diagnoses)} issues, attempting fixes...")
            fixes_applied = 0
            for diagnosis in diagnoses:
                if diagnosis.severity > 0.5:
                    success = await self._apply_fix(diagnosis, metrics)
                    if success:
                        fixes_applied += 1
            self.log(f"  - Applied {fixes_applied} fixes")
            
            # Step 5: Suggest new rewards if gaps remain
            self.log("Checking for skill gaps...")
            gaps = self.suggestion_engine.identify_skill_gaps()
            if gaps and iteration % 5 == 0:  # Every 5 iterations
                self.log(f"  - Found {len(gaps)} skill gaps")
                for gap in gaps[:2]:  # Limit to 2 new rewards per iteration
                    success = await self._create_and_test_reward(gap)
                    if success:
                        self.log(f"  - Created new reward for {gap}")
            
            # Step 6: Optimize weights
            self.log("Optimizing reward weights with GA...")
            optimized_weights = self.ga_optimizer.OptimizeWeights(metrics)
            self.apply_weights(optimized_weights)
            self.log("  - Weights optimized")
            
            # Step 7: Check termination condition
            if self._should_end_phase(metrics):
                self.log(f"Phase complete! Final metrics:")
                self._log_final_metrics(metrics)
                break
            
            iteration += 1
            self.log(f"Iteration {iteration} complete\n")
        
        self.log(f"Completed {iteration} iterations for {phase}\n")
    
    async def _apply_fix(
        self,
        diagnosis: RewardDiagnosis,
        all_metrics: Dict[str, RewardMetrics]
    ) -> bool:
        """
        Apply a single fix generated by LLM
        """
        self.log(f"  Fixing {diagnosis.rewardName}: {diagnosis.problemType}")
        
        try:
            code, compiles = await self.llm_generator.fix_reward(
                diagnosis,
                all_metrics[diagnosis.rewardName],
                self.load_reward_code(diagnosis.rewardName),
                self.get_similar_working_rewards(diagnosis.rewardName)
            )
            
            if not compiles:
                self.log(f"    ✗ Generated code didn't compile, skipping")
                return False
            
            # Test it
            validation = self.validator.ValidateRuntime(
                CreateRewardFromCode(code),
                test_episodes=50
            )
            
            if validation.performsBetter:
                self.log(f"    ✓ Fix validated and improves performance")
                self.save_reward_code(diagnosis.rewardName, code)
                return True
            else:
                self.log(f"    ✗ Fix didn't improve performance, skipping")
                return False
                
        except Exception as e:
            self.log(f"    ✗ Error applying fix: {str(e)}")
            return False
    
    async def _create_and_test_reward(self, skill_gap: str) -> bool:
        """
        Create a new reward for a skill gap
        """
        self.log(f"  Creating new reward for {skill_gap}")
        
        try:
            prompt = self.suggestion_engine.generate_reward_implementation_prompt(skill_gap)
            reward_name, code, compiles = await self.llm_generator.create_new_reward(
                skill_gap,
                str(self.config.current_phase),
                self.get_well_performing_rewards()
            )
            
            if not compiles:
                self.log(f"    ✗ New reward didn't compile")
                return False
            
            validation = self.validator.ValidateRuntime(
                CreateRewardFromCode(code),
                test_episodes=50
            )
            
            if validation.performsBetter:
                self.log(f"    ✓ New reward {reward_name} created and validated")
                self.save_new_reward(reward_name, code)
                self.add_to_reward_config(reward_name, initial_weight=5.0)
                return True
            else:
                self.log(f"    ✗ New reward didn't improve performance")
                return False
                
        except Exception as e:
            self.log(f"    ✗ Error creating new reward: {str(e)}")
            return False
    
    def run_training_subprocess(self, steps: int):
        """
        Execute training in a subprocess to isolate crashes
        """
        subprocess.run(
            ["./build/GigaLearnBot"],
            timeout=3600,
            capture_output=True
        )
    
    def _should_end_phase(self, metrics: Dict[str, RewardMetrics]) -> bool:
        """
        Determine if the phase is complete
        """
        # Phase ends when:
        # 1. Win rate is above threshold for this phase
        # 2. All major rewards are converged (stable metrics)
        # 3. No more low-hanging fruit improvements
        
        win_rate = self.get_current_win_rate()
        phase_threshold = self.get_phase_threshold(self.config.current_phase)
        
        if win_rate > phase_threshold:
            return True
        
        return False
    
    def log(self, message: str):
        """Log with timestamp"""
        timestamp = datetime.now().isoformat()
        self.log_file.write(f"[{timestamp}] {message}\n")
        print(f"[{timestamp}] {message}")
        self.log_file.flush()

# Main entry point
async def main():
    config = TrainingConfig(
        steps_per_eval=2_000_000_000,  # 2B steps between evaluations
        current_phase=TrainingPhase.PHASE_4_PLATINUM,
        max_phase_iterations=50,
        llm_model="claude-opus-4-5"
    )
    
    orchestrator = AutomatedTrainingOrchestrator(config)
    
    # Run the phase
    await orchestrator.run_training_phase(config.current_phase)
    
    # When done, can move to next phase
    print("Phase complete! Ready for next phase or deployment.")

if __name__ == "__main__":
    asyncio.run(main())
```

---

## Key Design Decisions

### 1. **Why Metrics-First Heuristics Before LLM?**

The heuristic layer acts as a filter:
- **Efficiency**: Detects 80% of issues with cheap rules (no LLM calls)
- **Safety**: Only sends well-characterized problems to LLM
- **Transparency**: You can audit the heuristics and understand the reasoning
- **Cost**: LLM calls are reserved for actual code generation, not diagnosis

### 2. **Why Validation Before Deployment?**

Three-tier validation prevents broken code from entering training:
1. **Compilation**: Catches type errors, syntax issues immediately
2. **Runtime**: Runs in isolated environment, detects crashes/NaN values
3. **Behavioral**: Compares to baseline, ensures actual improvement

### 3. **Why Genetic Algorithm for Weights, Not Heuristics?**

- Heuristics work well for diagnosis (clear rules)
- But weight optimization is multi-objective with complex interactions
- GA naturally handles Pareto frontiers (can't improve win rate without hurting dribbling)

### 4. **Why Proactive Suggestion Engine?**

Rather than wait for problems, the suggestion engine:
- Identifies skill gaps from training data
- Prompts LLM to design specific rewards
- Gets tested before integration
- Reduces manual intervention even further

---

## Practical Implementation Order

**Phase 1: Metrics & Diagnosis** (Week 1)
1. Implement `RewardMetricsCollector` - hook into training loop
2. Build `RewardDiagnostician` with 5-10 heuristic rules
3. Create simple logging/dashboard
4. Run on current Phase 5 training, validate diagnoses are accurate

**Phase 2: LLM Integration** (Week 2-3)
1. Implement `RewardPromptGenerator` with structured prompts
2. Test with simple reward fixes (changing thresholds, weights)
3. Add compilation testing
4. Test with 3-5 manually identified problems

**Phase 3: Validation Framework** (Week 3)
1. Build `RewardValidator` with isolated testing
2. Integrate with training loop
3. Test false positive rate (invalid fixes that seem good)

**Phase 4: Full Orchestration** (Week 4+)
1. Connect all tiers in `AutomatedTrainingOrchestrator`
2. Add skill gap detection
3. Run hands-off for a full phase, monitor closely
4. Adjust thresholds based on real performance

---

## Monitoring & Safety

```python
class AutomationMonitor:
    """
    Ensures the system doesn't break training
    """
    
    def __init__(self):
        self.alert_thresholds = {
            "compilation_failure_rate": 0.2,      # >20% fails = alert
            "validation_failure_rate": 0.5,       # >50% don't help = alert
            "win_rate_drop": 0.05,                # >5% drop = alert
            "reward_creation_rate": 1.0,          # >1 per iteration = slow down
        }
    
    def check_system_health(self, metrics: Dict) -> None:
        for metric, threshold in self.alert_thresholds.items():
            if metrics[metric] > threshold:
                raise AutomationCriticalError(
                    f"{metric} exceeded threshold: {metrics[metric]} > {threshold}"
                )
    
    def get_system_status(self) -> Dict:
        return {
            "compilation_success_rate": ...,
            "validation_success_rate": ...,
            "avg_win_rate": ...,
            "recent_rewards_added": ...,
            "recent_rewards_removed": ...,
        }
```

---

## Expected Outcomes

If implemented successfully:

1. **Reduce manual reward engineering from 40% to <10% of development time**
   - GA handles weight balancing (automatic)
   - LLM handles reward fixes (automatic)
   - Heuristics catch problems early (no debugging needed)

2. **Faster iteration on new mechanics**
   - Instead of "code reward → test → debug → balance"
   - Just: "identify gap → AI codes → AI validates → automatically integrated"

3. **Better reward interactions**
   - Heuristics detect conflicts before they become problems
   - Genetic algorithm finds better weight combinations than manual tuning

4. **More reliable training**
   - Broken rewards caught before deployment
   - Compilation errors found immediately
   - Runtime crashes detected in validation

5. **Continuous improvement**
   - System automatically discovers and fixes issues
   - No need to monitor training as closely
   - Can scale to larger reward sets (50+ simultaneously)

---

## Example: End-to-End Flow

**Scenario**: Bot struggles with demo execution in Phase 4

1. **Metrics Collection** → Notices DemoReward has:
   - 12% activation rate (should be 20%)
   - High variance (0.85)
   - Conflicts with BumpReward

2. **Diagnosis** → Heuristic Engine determines:
   - Problem: GRADIENT_CONFLICT
   - Severity: 0.7
   - Suggested fix: Restructure reward with state gating

3. **Code Generation** → LLM receives:
   - Current DemoReward code
   - Conflict metrics (BumpReward overlap = 0.65)
   - Reference to working BumpReward implementation
   - Prompt: "Make these mutually exclusive"

4. **LLM Output** → Generates:
   ```cpp
   struct ContextualDemoReward : public Reward {
     float Evaluate(...) {
       // Only reward demos when:
       // 1) Opponent is visible ahead
       // 2) Bump distance is too far (>50 UPS)
       // This prevents conflict with BumpReward
     }
   };
   ```

5. **Validation** → Tests show:
   - Compiles ✓
   - Doesn't crash ✓
   - Demo success rate +15% ✓
   - Win rate +2% ✓

6. **Deployment** → Code automatically:
   - Replaces old DemoReward
   - GA re-optimizes weights
   - Training continues with better signal

**Total time**: 30 minutes (mostly training running in background)
**Manual intervention needed**: Zero

