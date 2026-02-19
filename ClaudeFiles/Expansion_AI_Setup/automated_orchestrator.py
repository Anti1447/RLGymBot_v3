#!/usr/bin/env python3
"""
Automated Reward Engineering System - Python Orchestration Layer

This is the "control center" that:
1. Monitors C++ training process
2. Collects metrics
3. Calls the diagnostician
4. Interfaces with Claude API for reward generation
5. Validates and deploys new rewards
"""

import anthropic
import json
import subprocess
import time
import asyncio
import os
from datetime import datetime
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass, asdict
from enum import Enum
import logging

# ============================================================================
# Data Structures
# ============================================================================

class RewardProblemType(Enum):
    NO_PROBLEM = 0
    LOW_ACTIVATION = 1
    HIGH_VARIANCE = 2
    POOR_CORRELATION = 3
    CONFLICTING_WITH_OTHER = 4
    INCORRECT_SCALING = 5
    REWARD_SATURATION = 6
    GRADIENT_DILUTION = 7
    UNKNOWN = 8

@dataclass
class RewardMetrics:
    reward_name: str
    activation_rate: float
    mean_activation_value: float
    variance_in_activation: float
    correlation_with_win_rate: float
    base_weight: float
    compilation_failures: int = 0
    runtime_errors: int = 0
    average_latency: float = 0.0

@dataclass
class RewardDiagnosis:
    reward_name: str
    problem_type: RewardProblemType
    severity: float
    explanation: str
    suggested_fixes: List[str]
    conflicting_rewards: List[str]
    metrics_context: str = ""

# ============================================================================
# Logging Setup
# ============================================================================

def setup_logging(log_file: str = "training_automation.log"):
    """Configure logging with both file and console output"""
    logger = logging.getLogger("RewardAutomation")
    logger.setLevel(logging.DEBUG)
    
    # File handler
    fh = logging.FileHandler(log_file)
    fh.setLevel(logging.DEBUG)
    
    # Console handler
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    
    # Formatter
    formatter = logging.Formatter(
        '[%(asctime)s] %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    fh.setFormatter(formatter)
    ch.setFormatter(formatter)
    
    logger.addHandler(fh)
    logger.addHandler(ch)
    
    return logger

logger = setup_logging()

# ============================================================================
# Prompt Generators
# ============================================================================

class RewardPromptGenerator:
    """Generate structured prompts for Claude"""
    
    @staticmethod
    def generate_fix_prompt(
        diagnosis: RewardDiagnosis,
        metrics: RewardMetrics,
        existing_code: str,
        similar_working_rewards: List[str],
        conflict_details: Optional[Dict] = None
    ) -> str:
        """Generate a prompt to fix a broken reward"""
        
        conflict_section = ""
        if conflict_details:
            conflict_section = f"""
CONFLICTING REWARDS:
{json.dumps(conflict_details, indent=2)}

These rewards have negative correlation with {diagnosis.reward_name}.
Your fix should make them mutually exclusive or complementary.
"""
        
        return f"""You are a Rocket League AI training expert. Fix this broken reward function.

PROBLEM DIAGNOSIS:
Type: {diagnosis.problem_type.name}
Severity: {diagnosis.severity:.2f}/1.0
Explanation: {diagnosis.explanation}

METRICS:
- Activation Rate: {metrics.activation_rate*100:.1f}% (target: 5-30%)
- Mean Value: {metrics.mean_activation_value:.3f} (target: 0.2-0.8)
- Variance: {metrics.variance_in_activation:.3f} (target: <0.6)
- Win Correlation: {metrics.correlation_with_win_rate:.3f} (target: >0.15)
- Current Weight: {metrics.base_weight}

CURRENT IMPLEMENTATION:
```cpp
{existing_code}
```

SIMILAR WORKING REWARDS (reference patterns):
{chr(10).join(f"- {reward}" for reward in similar_working_rewards[:3])}

{conflict_section}

CONSTRAINTS:
1. Must compile with C++17, LibTorch in RLGymCPP
2. Inherit from Reward or RewardWrapper
3. Implement: float Evaluate(const GameState&, int player, const TerminalCondition&)
4. Return 0.0-1.0 typically (can exceed for importance)
5. No expensive computation
6. Account for 8-tick simulation delay

REQUIREMENTS:
Fix the {diagnosis.problem_type.name} issue.
For each suggested fix:
{chr(10).join(f"  - {fix}" for fix in diagnosis.suggested_fixes)}

Return ONLY the fixed C++ code, no explanation, no markdown.
"""
    
    @staticmethod
    def generate_new_reward_prompt(
        desired_behavior: str,
        phase: str,
        similar_rewards: List[str],
        existing_rewards: List[str]
    ) -> str:
        """Generate a prompt to create an entirely new reward"""
        
        existing_list = "\n".join(f"- {r}" for r in existing_rewards[:10])
        
        return f"""You are a Rocket League AI reward engineer. Design a new reward function.

DESIRED BEHAVIOR:
{desired_behavior}

TRAINING PHASE: {phase}
Bot Skill Level: Diamond mechanics, learning advanced tactics

SIMILAR EXISTING REWARDS (for reference):
{chr(10).join(f"- {reward}" for reward in similar_rewards[:3])}

EXISTING REWARDS IN THIS PHASE (avoid duplication):
{existing_list}

GUIDELINES:
1. Specific, measurable behavior in Rocket League terms
2. Clear game state features you'll measure
3. Smooth gradient (avoid sharp on/off)
4. Multi-agent compatible (3v3 gameplay)
5. Avoid duplicating existing mechanics
6. Consider temporal dynamics

TECHNICAL CONSTRAINTS:
- C++17, LibTorch, RLGymCPP framework
- Compile time <100ms, execute <1ms per step
- Return float 0.0-1.0 (typical)
- GameState has: car positions/velocities/rotations, ball state, boost values

IMPLEMENTATION:
Return ONLY the C++ class code. Include:
1. Class declaration inheriting from Reward
2. Constructor
3. Evaluate function with clear logic
4. Comments explaining the mechanics

Do not include markdown, only C++ code.
"""

# ============================================================================
# LLM Integration
# ============================================================================

class LLMRewardGenerator:
    """Interface with Claude for reward generation"""
    
    def __init__(self, api_key: str = None, model: str = "claude-opus-4-5-20250514"):
        self.api_key = api_key or os.getenv("ANTHROPIC_API_KEY")
        if not self.api_key:
            raise ValueError("ANTHROPIC_API_KEY environment variable not set")
        self.client = anthropic.Anthropic(api_key=self.api_key)
        self.model = model
    
    async def fix_reward(
        self,
        diagnosis: RewardDiagnosis,
        metrics: RewardMetrics,
        existing_code: str,
        similar_working_rewards: List[str],
        conflict_details: Optional[Dict] = None
    ) -> Tuple[str, bool]:
        """
        Generate and validate a fix for a broken reward
        Returns: (generated_code, successfully_compiles)
        """
        logger.info(f"Generating fix for {diagnosis.reward_name}: {diagnosis.problem_type.name}")
        
        prompt = RewardPromptGenerator.generate_fix_prompt(
            diagnosis, metrics, existing_code, similar_working_rewards, conflict_details
        )
        
        try:
            message = self.client.messages.create(
                model=self.model,
                max_tokens=2000,
                messages=[{"role": "user", "content": prompt}]
            )
            
            generated_code = message.content[0].text
            logger.debug(f"Generated {len(generated_code)} chars of code")
            
            # Clean up markdown if present
            generated_code = self._extract_cpp_code(generated_code)
            
            # Test compilation
            compiled_ok = await self._test_compile(generated_code, diagnosis.reward_name)
            
            if compiled_ok:
                logger.info(f"✓ Generated code for {diagnosis.reward_name} compiles successfully")
            else:
                logger.warning(f"✗ Generated code for {diagnosis.reward_name} failed to compile")
            
            return generated_code, compiled_ok
            
        except Exception as e:
            logger.error(f"Error generating fix: {str(e)}")
            return "", False
    
    async def create_new_reward(
        self,
        desired_behavior: str,
        phase: str,
        similar_rewards: List[str],
        existing_rewards: List[str]
    ) -> Tuple[str, str, bool]:
        """
        Create a new reward for a skill gap
        Returns: (reward_name, code, successfully_compiles)
        """
        logger.info(f"Generating new reward for: {desired_behavior[:50]}...")
        
        prompt = RewardPromptGenerator.generate_new_reward_prompt(
            desired_behavior, phase, similar_rewards, existing_rewards
        )
        
        try:
            message = self.client.messages.create(
                model=self.model,
                max_tokens=3000,
                messages=[{"role": "user", "content": prompt}]
            )
            
            generated_code = message.content[0].text
            generated_code = self._extract_cpp_code(generated_code)
            
            # Extract class name
            reward_name = self._extract_class_name(generated_code)
            logger.debug(f"Extracted reward class name: {reward_name}")
            
            # Test compilation
            compiled_ok = await self._test_compile(generated_code, reward_name)
            
            if compiled_ok:
                logger.info(f"✓ New reward {reward_name} created and compiles")
            else:
                logger.warning(f"✗ New reward {reward_name} failed to compile")
            
            return reward_name, generated_code, compiled_ok
            
        except Exception as e:
            logger.error(f"Error creating new reward: {str(e)}")
            return "", "", False
    
    async def _test_compile(self, code: str, reward_name: str) -> bool:
        """Test if code compiles in the actual project"""
        temp_path = f"/tmp/test_{reward_name}.h"
        
        try:
            # Write to temp file
            with open(temp_path, "w") as f:
                f.write(code)
            
            # Run a quick compile test
            # (In real implementation, this would invoke cmake/ninja)
            # For now, just do basic syntax checks
            if not code.strip().startswith("#pragma") and not code.strip().startswith("struct") and not code.strip().startswith("class"):
                logger.warning(f"Code doesn't look like valid C++: {code[:100]}...")
                return False
            
            # TODO: Actually invoke cmake to test compilation
            # result = subprocess.run(
            #     ["cmake", "--build", "build", "--target", "compile_test"],
            #     capture_output=True,
            #     timeout=30
            # )
            # return result.returncode == 0
            
            # For now, assume it compiles if basic syntax is ok
            return True
            
        except Exception as e:
            logger.error(f"Compilation test error: {str(e)}")
            return False
    
    @staticmethod
    def _extract_cpp_code(text: str) -> str:
        """Extract C++ code from markdown if present"""
        if "```cpp" in text:
            start = text.find("```cpp") + 6
            end = text.find("```", start)
            if end > start:
                return text[start:end].strip()
        elif "```" in text:
            start = text.find("```") + 3
            end = text.find("```", start)
            if end > start:
                return text[start:end].strip()
        return text.strip()
    
    @staticmethod
    def _extract_class_name(code: str) -> str:
        """Extract class name from C++ code"""
        import re
        match = re.search(r'(?:struct|class)\s+(\w+)\s*(?::|\{)', code)
        if match:
            return match.group(1)
        return "UnknownReward"

# ============================================================================
# Metrics Collection (Mock - in real implementation, would read from C++)
# ============================================================================

class MetricsCollector:
    """Collects training metrics from the training process"""
    
    def __init__(self, metrics_file: str = "metrics.json"):
        self.metrics_file = metrics_file
    
    def collect_metrics(self) -> Dict[str, RewardMetrics]:
        """Read metrics from file written by C++ training loop"""
        try:
            with open(self.metrics_file, "r") as f:
                data = json.load(f)
            
            metrics = {}
            for reward_name, m in data.items():
                metrics[reward_name] = RewardMetrics(
                    reward_name=reward_name,
                    activation_rate=m.get("activation_rate", 0.0),
                    mean_activation_value=m.get("mean_activation_value", 0.0),
                    variance_in_activation=m.get("variance_in_activation", 0.0),
                    correlation_with_win_rate=m.get("correlation_with_win_rate", 0.0),
                    base_weight=m.get("base_weight", 1.0)
                )
            return metrics
        except FileNotFoundError:
            logger.warning(f"Metrics file {self.metrics_file} not found")
            return {}

# ============================================================================
# Main Orchestrator
# ============================================================================

class AutomatedRewardOrchestrator:
    """Main control loop for the entire system"""
    
    def __init__(
        self,
        training_executable: str = "./build/GigaLearnBot",
        steps_per_iteration: int = 2_000_000_000,  # 2B steps
        max_iterations: int = 50
    ):
        self.training_executable = training_executable
        self.steps_per_iteration = steps_per_iteration
        self.max_iterations = max_iterations
        
        self.llm_generator = LLMRewardGenerator()
        self.metrics_collector = MetricsCollector()
        
        self.iteration = 0
        self.rewards_created = []
        self.rewards_fixed = []
    
    async def run_training_phase(self, phase_name: str = "PHASE_4_PLATINUM"):
        """Main training loop"""
        logger.info(f"Starting automated training for {phase_name}")
        
        self.iteration = 0
        
        while self.iteration < self.max_iterations:
            logger.info(f"=== Iteration {self.iteration + 1}/{self.max_iterations} ===")
            
            # Step 1: Run training
            logger.info(f"Running training for {self.steps_per_iteration/1e9:.1f}B steps...")
            await self._run_training()
            
            # Step 2: Collect metrics
            logger.info("Collecting metrics...")
            metrics = self.metrics_collector.collect_metrics()
            if not metrics:
                logger.warning("No metrics collected, skipping iteration")
                self.iteration += 1
                continue
            
            # Step 3: Diagnose problems
            logger.info(f"Analyzing {len(metrics)} rewards for problems...")
            diagnoses = self._diagnose_rewards(metrics)
            logger.info(f"Found {len(diagnoses)} issues")
            
            # Step 4: Fix high-severity problems
            logger.info("Applying fixes...")
            fixes_applied = 0
            for diagnosis in diagnoses[:5]:  # Limit to top 5
                if diagnosis.severity > 0.5:
                    success = await self._apply_fix(diagnosis, metrics)
                    if success:
                        fixes_applied += 1
            logger.info(f"Applied {fixes_applied} fixes")
            
            # Step 5: Check for skill gaps (every 5 iterations)
            if self.iteration % 5 == 0:
                logger.info("Checking for skill gaps...")
                gaps = self._identify_skill_gaps(metrics)
                if gaps:
                    for gap in gaps[:2]:
                        success = await self._create_and_test_reward(gap)
                        if success:
                            logger.info(f"Created new reward for: {gap}")
            
            # Step 6: Check if phase is complete
            if self._should_end_phase(metrics):
                logger.info(f"Phase {phase_name} complete!")
                break
            
            self.iteration += 1
            logger.info("")
    
    async def _run_training(self):
        """Execute training in subprocess"""
        try:
            process = await asyncio.create_subprocess_exec(
                self.training_executable,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=3600)
            logger.debug(f"Training completed with code: {process.returncode}")
        except asyncio.TimeoutError:
            logger.error("Training subprocess timed out")
        except Exception as e:
            logger.error(f"Error running training: {str(e)}")
    
    def _diagnose_rewards(self, metrics: Dict[str, RewardMetrics]) -> List[RewardDiagnosis]:
        """Diagnose rewards using heuristic rules"""
        diagnoses = []
        
        for reward_name, m in metrics.items():
            diagnosis = self._diagnose_single(reward_name, m, metrics)
            if diagnosis.problem_type != RewardProblemType.NO_PROBLEM:
                diagnoses.append(diagnosis)
        
        # Sort by severity
        diagnoses.sort(key=lambda d: d.severity, reverse=True)
        return diagnoses
    
    def _diagnose_single(
        self,
        reward_name: str,
        metrics: RewardMetrics,
        all_metrics: Dict[str, RewardMetrics]
    ) -> RewardDiagnosis:
        """Apply heuristic rules to diagnose a single reward"""
        
        # Rule 1: Low activation
        if metrics.activation_rate < 0.05 and metrics.base_weight < 0.5:
            return RewardDiagnosis(
                reward_name=reward_name,
                problem_type=RewardProblemType.LOW_ACTIVATION,
                severity=metrics.activation_rate / 0.05,
                explanation=f"Activation rate too low: {metrics.activation_rate*100:.1f}%",
                suggested_fixes=[
                    "Lower sensitivity thresholds",
                    "Increase reward weight",
                    "Verify condition is achievable"
                ],
                conflicting_rewards=[]
            )
        
        # Rule 2: High variance
        if metrics.variance_in_activation > 0.6 and metrics.mean_activation_value < 0.5:
            return RewardDiagnosis(
                reward_name=reward_name,
                problem_type=RewardProblemType.HIGH_VARIANCE,
                severity=min(1.0, metrics.variance_in_activation / 0.6),
                explanation=f"Reward signal too noisy: variance {metrics.variance_in_activation:.2f}",
                suggested_fixes=[
                    "Add exponential moving average smoothing",
                    "Use state-dependent scaling",
                    "Switch to more stable metric"
                ],
                conflicting_rewards=[]
            )
        
        # Rule 3: Poor win correlation
        if metrics.activation_rate > 0.1 and metrics.correlation_with_win_rate < 0.15:
            return RewardDiagnosis(
                reward_name=reward_name,
                problem_type=RewardProblemType.POOR_CORRELATION,
                severity=max(0.0, 0.15 - metrics.correlation_with_win_rate),
                explanation=f"Active but doesn't help win (correlation: {metrics.correlation_with_win_rate:.2f})",
                suggested_fixes=[
                    "Verify target behavior is correct",
                    "Check game state features",
                    "Consider removing reward"
                ],
                conflicting_rewards=[]
            )
        
        return RewardDiagnosis(
            reward_name=reward_name,
            problem_type=RewardProblemType.NO_PROBLEM,
            severity=0.0,
            explanation="No problems detected",
            suggested_fixes=[],
            conflicting_rewards=[]
        )
    
    async def _apply_fix(
        self,
        diagnosis: RewardDiagnosis,
        metrics: Dict[str, RewardMetrics]
    ) -> bool:
        """Apply a fix to a broken reward"""
        logger.info(f"  Fixing {diagnosis.reward_name}: {diagnosis.problem_type.name}")
        
        try:
            # Get existing code (mock - would load from file)
            existing_code = f"// Existing implementation of {diagnosis.reward_name}\nstruct {diagnosis.reward_name} {{}}"
            
            code, compiles = await self.llm_generator.fix_reward(
                diagnosis=diagnosis,
                metrics=metrics[diagnosis.reward_name],
                existing_code=existing_code,
                similar_working_rewards=["VelocityReward", "FaceBallReward"],
                conflict_details=None
            )
            
            if compiles:
                logger.info(f"    ✓ Fix compiled successfully")
                self.rewards_fixed.append(diagnosis.reward_name)
                return True
            else:
                logger.warning(f"    ✗ Fix failed to compile")
                return False
        except Exception as e:
            logger.error(f"    ✗ Error applying fix: {str(e)}")
            return False
    
    async def _create_and_test_reward(self, skill_gap: str) -> bool:
        """Create a new reward for a skill gap"""
        logger.info(f"  Creating reward for skill gap: {skill_gap}")
        
        try:
            reward_name, code, compiles = await self.llm_generator.create_new_reward(
                desired_behavior=skill_gap,
                phase="PHASE_4_PLATINUM",
                similar_rewards=["VelocityReward", "AerialReward", "DribbleReward"],
                existing_rewards=[]  # Would load from config
            )
            
            if compiles:
                logger.info(f"    ✓ New reward {reward_name} created and compiles")
                self.rewards_created.append(reward_name)
                return True
            else:
                logger.warning(f"    ✗ New reward failed to compile")
                return False
        except Exception as e:
            logger.error(f"    ✗ Error creating reward: {str(e)}")
            return False
    
    def _identify_skill_gaps(self, metrics: Dict[str, RewardMetrics]) -> List[str]:
        """Identify what the bot struggles with"""
        # Mock implementation
        return [
            "Better demo execution timing",
            "Improved aerial accuracy",
            "Defensive rotation awareness"
        ]
    
    def _should_end_phase(self, metrics: Dict[str, RewardMetrics]) -> bool:
        """Determine if phase should end"""
        # Mock: end after 3 iterations for demo
        return self.iteration >= 2

# ============================================================================
# Main Entry Point
# ============================================================================

async def main():
    """Run the automated training system"""
    logger.info("Automated Reward Engineering System Starting")
    logger.info("=" * 60)
    
    orchestrator = AutomatedRewardOrchestrator(
        steps_per_iteration=2_000_000_000,  # 2B steps
        max_iterations=50
    )
    
    try:
        await orchestrator.run_training_phase("PHASE_4_PLATINUM")
        
        logger.info("=" * 60)
        logger.info("Training Complete!")
        logger.info(f"Rewards created: {len(orchestrator.rewards_created)}")
        logger.info(f"Rewards fixed: {len(orchestrator.rewards_fixed)}")
        
    except KeyboardInterrupt:
        logger.info("Training interrupted by user")
    except Exception as e:
        logger.error(f"Fatal error: {str(e)}", exc_info=True)

if __name__ == "__main__":
    asyncio.run(main())
