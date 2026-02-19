#!/usr/bin/env python3
"""
Phase 3-4: LLM-Based Reward Generation & Compilation Testing
Standalone script — reads checkpoint diagnostics, generates/fixes C++ rewards via Claude API.

Usage:
    python reward_llm_generator.py [--checkpoint PATH] [--checkpoints-root PATH]
                                   [--mode fix|new|both] [--max-fixes N]
                                   [--dry-run] [--no-compile-test]
                                   [--model MODEL]
"""

import json
import os
import re
import subprocess
import sys
import time
import argparse
import logging
from pathlib import Path
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass, asdict, field
from datetime import datetime

# ============================================================================
# PATH CONSTANTS
# ============================================================================

# Determine project root relative to this script
# Script lives in GigaLearnBot/python_scripts/ OR build/python_scripts/
_SCRIPT_DIR = Path(__file__).resolve().parent
_POSSIBLE_ROOTS = [
    _SCRIPT_DIR.parent,                          # python_scripts/../ = GigaLearnBot/
    _SCRIPT_DIR.parent.parent,                   # build/python_scripts/../../ = GigaLearnBot/
]

PROJECT_ROOT = None
for _root in _POSSIBLE_ROOTS:
    if (_root / "GigaLearnCPP").is_dir():
        PROJECT_ROOT = _root
        break
if PROJECT_ROOT is None:
    PROJECT_ROOT = _SCRIPT_DIR.parent  # Fallback

REWARDS_DIR = PROJECT_ROOT / "GigaLearnCPP" / "RLGymCPP" / "src" / "RLGymCPP" / "Rewards"
GENERATED_DIR = PROJECT_ROOT / "build" / "generated_rewards"
STAGING_DIR = GENERATED_DIR / "staging"
COMPILE_TEST_DIR = GENERATED_DIR / "compile_test"
DEFAULT_CHECKPOINTS_ROOT = PROJECT_ROOT / "build" / "checkpoints"

# Cost tracking
COST_LOG_FILE = GENERATED_DIR / "api_cost_log.json"
MAX_DAILY_COST_USD = 5.0

# Model configuration
DEFAULT_MODEL = "claude-sonnet-4-20250514"
MAX_TOKENS_FIX = 4000
MAX_TOKENS_NEW = 6000
MAX_COMPILE_RETRIES = 3

logger = logging.getLogger("RewardLLMGenerator")

# ============================================================================
# DATA STRUCTURES
# ============================================================================

@dataclass
class GenerationRequest:
    """A request to generate or fix a reward."""
    reward_name: str
    mode: str                       # "fix" or "new"
    problem_type: str               # From REWARD_DIAGNOSIS.json
    severity: float
    explanation: str
    suggested_fixes: List[str]
    existing_code: Optional[str]    # The current .h file content (for fixes)
    metrics: Optional[Dict]         # From REWARD_METRICS.json
    similar_rewards: List[str]      # Names of similar working rewards

@dataclass
class GenerationResult:
    """Result of a generation attempt."""
    reward_name: str
    class_name: str
    mode: str
    success: bool
    generated_code: str
    compiled: bool
    compile_errors: List[str]
    attempts: int
    api_cost_usd: float
    timestamp: str
    output_file: Optional[str]
    notes: str = ""

@dataclass
class GenerationManifest:
    """Manifest of all generation results for a run."""
    checkpoint_path: str
    total_timesteps: int
    run_timestamp: str
    results: List[GenerationResult] = field(default_factory=list)
    total_api_cost_usd: float = 0.0


# ============================================================================
# REWARD SOURCE FINDER
# ============================================================================

class RewardSourceFinder:
    """Finds C++ source code for rewards by class name."""

    def __init__(self, rewards_dir: Path = REWARDS_DIR):
        self.rewards_dir = rewards_dir
        self._class_to_file: Dict[str, Tuple[Path, str]] = {}
        self._build_index()

    def _build_index(self):
        """Scan all .h files and map class/struct names to files."""
        class_pattern = re.compile(
            r'(?:class|struct)\s+(\w+)\s*(?::\s*public\s+\w+)',
        )
        typedef_pattern = re.compile(
            r'typedef\s+\w+<[^>]*>\s+(\w+);'
        )

        for h_file in self.rewards_dir.glob("*.h"):
            try:
                content = h_file.read_text(encoding='utf-8', errors='replace')
            except Exception:
                continue

            for match in class_pattern.finditer(content):
                class_name = match.group(1)
                self._class_to_file[class_name] = (h_file, content)

            for match in typedef_pattern.finditer(content):
                class_name = match.group(1)
                self._class_to_file[class_name] = (h_file, content)

    def find_source(self, reward_name: str) -> Optional[Tuple[Path, str]]:
        """Return (file_path, full_file_content) for a reward class name."""
        # Direct match
        if reward_name in self._class_to_file:
            return self._class_to_file[reward_name]

        # Try common suffixes
        for suffix in ["Reward", "Penalty", ""]:
            base = reward_name
            for strip_suffix in ["Reward", "Penalty"]:
                if base.endswith(strip_suffix):
                    base = base[:-len(strip_suffix)]
                    break
            candidate = base + suffix
            if candidate in self._class_to_file:
                return self._class_to_file[candidate]

        # Template reward names (e.g., "PlayerDataEventReward<1,0>")
        if '<' in reward_name:
            base_name = reward_name.split('<')[0]
            if base_name in self._class_to_file:
                return self._class_to_file[base_name]

        return None

    def get_exemplar_rewards(self, category: str = "simple") -> List[Tuple[str, str]]:
        """Return (name, truncated_code) pairs for well-written reference rewards."""
        exemplars = {
            "simple": ["GoalReward", "SpeedScaledShotReward", "FaceBallReward"],
            "event_based": ["GoalReward", "SpeedScaledShotReward", "SpeedScaledSaveReward"],
            "stateful": ["DoubleJumpAerialReward", "WavedashBasicReward"],
            "complex": ["DoubleJumpAerialReward", "AdvancedRecoveryReward"],
        }
        results = []
        for name in exemplars.get(category, exemplars["simple"]):
            source = self.find_source(name)
            if source:
                # Truncate to keep prompt size reasonable
                lines = source[1].split('\n')[:100]
                results.append((name, '\n'.join(lines)))
        return results

    def get_all_reward_names(self) -> List[str]:
        """Return all known reward class names."""
        return list(self._class_to_file.keys())


# ============================================================================
# CODE VALIDATOR
# ============================================================================

class CodeValidator:
    """Static analysis of generated C++ code before compilation."""

    DANGEROUS_PATTERNS = [
        (r'\bwhile\s*\(\s*(?:true|1)\s*\)', "Infinite loop detected"),
        (r'\bfor\s*\(\s*;\s*;\s*\)', "Infinite loop detected"),
        (r'\bgoto\b', "goto statements not allowed"),
        (r'\bsystem\s*\(', "system() calls not allowed"),
        (r'\bexec[vl]?p?\s*\(', "exec calls not allowed"),
        (r'#include\s*[<"](?:fstream|ofstream|ifstream)[>"]', "File I/O not allowed in rewards"),
        (r'\bstd::thread\b', "Thread creation not allowed in rewards"),
    ]

    REQUIRED_PATTERNS = [
        (r'#pragma\s+once', "Missing #pragma once"),
        (r'namespace\s+RLGC', "Must be in namespace RLGC"),
        (r':\s*public\s+(?:RLGC::)?Reward\b', "Must inherit from Reward"),
        (r'GetReward\s*\(', "Must implement GetReward()"),
    ]

    NAN_WARNING_PATTERNS = [
        (r'\b0\.0f?\s*/\s*0\.0f?\b', "Possible division by zero producing NaN"),
        (r'sqrtf?\s*\(\s*-', "sqrt of negative value"),
    ]

    @classmethod
    def validate(cls, code: str) -> Tuple[bool, List[str]]:
        """Validate generated code. Returns (valid, list_of_issues)."""
        issues = []

        # Check dangerous patterns
        for pattern, message in cls.DANGEROUS_PATTERNS:
            if re.search(pattern, code):
                issues.append(f"BLOCKED: {message}")

        # Check required patterns
        for pattern, message in cls.REQUIRED_PATTERNS:
            if not re.search(pattern, code):
                issues.append(f"MISSING: {message}")

        # Check NaN patterns (warnings only)
        for pattern, message in cls.NAN_WARNING_PATTERNS:
            if re.search(pattern, code):
                issues.append(f"WARNING: {message}")

        # Check code length
        if len(code.strip()) < 150:
            issues.append("BLOCKED: Generated code too short (< 150 chars)")

        # Check for balanced braces
        open_braces = code.count('{')
        close_braces = code.count('}')
        if open_braces != close_braces:
            issues.append(f"BLOCKED: Unbalanced braces ({open_braces} open, {close_braces} close)")

        has_blocking = any(
            issue.startswith("BLOCKED:") or issue.startswith("MISSING:")
            for issue in issues
        )
        return not has_blocking, issues


# ============================================================================
# COMPILATION TESTER
# ============================================================================

class CompilationTester:
    """Tests generated C++ reward headers for compilation correctness on Windows/MSVC."""

    def __init__(self, project_root: Path = PROJECT_ROOT):
        self.project_root = project_root
        self.rlgymcpp_src = project_root / "GigaLearnCPP" / "RLGymCPP" / "src"
        self.rocketsim_src = project_root / "GigaLearnCPP" / "RLGymCPP" / "RocketSim" / "src"
        self.json_include = project_root / "GigaLearnCPP" / "libsrc" / "json"
        self.thread_pool_include = project_root / "GigaLearnCPP" / "RLGymCPP" / "thread_pool"
        self.libtorch_include = project_root / "GigaLearnCPP" / "libtorch" / "include"

        self.compile_test_dir = COMPILE_TEST_DIR
        self.compile_test_dir.mkdir(parents=True, exist_ok=True)

        self._cl_exe_path: Optional[str] = None
        self._msvc_env: Optional[Dict] = None
        self._initialized = False

    def _lazy_init(self):
        """Lazy initialization — only find cl.exe when actually needed."""
        if self._initialized:
            return
        self._initialized = True
        self._cl_exe_path = self._find_cl_exe()
        if self._cl_exe_path:
            self._msvc_env = self._get_msvc_env()
            logger.info(f"CompilationTester: Found cl.exe at {self._cl_exe_path}")
        else:
            logger.warning("CompilationTester: cl.exe not found — compilation testing disabled")

    def _find_cl_exe(self) -> Optional[str]:
        """Find MSVC cl.exe, preferring the compiler used by the project's CMake build."""

        # Strategy 1: Read from CMakeCache.txt to find the exact compiler used
        cmake_cache = self.project_root / "build" / "CMakeCache.txt"
        if cmake_cache.exists():
            try:
                cache_text = cmake_cache.read_text(errors='replace')
                # Look for CMAKE_AR which contains the MSVC toolset path
                import re as _re
                match = _re.search(
                    r'CMAKE_AR:FILEPATH=(.+)/bin/Hostx64/x64/lib\.exe',
                    cache_text
                )
                if match:
                    tools_path = Path(match.group(1).strip())
                    cl_path = tools_path / "bin" / "Hostx64" / "x64" / "cl.exe"
                    if cl_path.exists():
                        logger.debug(f"Found cl.exe from CMakeCache: {cl_path}")
                        return str(cl_path)
            except Exception as e:
                logger.debug(f"CMakeCache parse failed: {e}")

        # Strategy 2: Use vswhere
        vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
        if vswhere.exists():
            vswhere_queries = [
                [str(vswhere), "-latest", "-products", "*",
                 "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                 "-property", "installationPath"],
                [str(vswhere), "-latest", "-prerelease", "-products", "*",
                 "-property", "installationPath"],
                [str(vswhere), "-prerelease", "-all", "-products", "*",
                 "-property", "installationPath"],
            ]

            for query in vswhere_queries:
                try:
                    result = subprocess.run(
                        query, capture_output=True, text=True, timeout=10
                    )
                    if result.returncode == 0 and result.stdout.strip():
                        for vs_line in result.stdout.strip().split('\n'):
                            vs_path = Path(vs_line.strip())
                            tools_dir = vs_path / "VC" / "Tools" / "MSVC"
                            if tools_dir.exists():
                                versions = sorted(tools_dir.iterdir(), reverse=True)
                                for ver in versions:
                                    cl_path = ver / "bin" / "Hostx64" / "x64" / "cl.exe"
                                    if cl_path.exists():
                                        return str(cl_path)
                except Exception as e:
                    logger.debug(f"vswhere query failed: {e}")

        # Strategy 3: try PATH
        try:
            result = subprocess.run(["where", "cl.exe"], capture_output=True, text=True, timeout=5)
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip().split('\n')[0].strip()
        except Exception:
            pass

        return None

    def _get_msvc_env(self) -> Optional[Dict]:
        """Get MSVC environment via vcvarsall.bat, with caching."""
        env_cache = self.compile_test_dir / ".msvc_env_cache.json"

        # Try cache
        if env_cache.exists():
            try:
                cached = json.loads(env_cache.read_text())
                if time.time() - cached.get("timestamp", 0) < 86400:  # 24h
                    return cached["env"]
            except Exception:
                pass

        # Find vcvarsall.bat
        vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
        if not vswhere.exists():
            return None

        vcvarsall = None

        # First try: derive VS install path from cl.exe path
        if self._cl_exe_path:
            # cl.exe is at .../VC/Tools/MSVC/xx.xx/bin/Hostx64/x64/cl.exe
            # vcvarsall is at .../VC/Auxiliary/Build/vcvarsall.bat
            cl_path = Path(self._cl_exe_path)
            # Walk up to find VC directory
            for parent in cl_path.parents:
                if parent.name == "VC":
                    candidate = parent / "Auxiliary" / "Build" / "vcvarsall.bat"
                    if candidate.exists():
                        vcvarsall = candidate
                        break

        # Second try: use vswhere
        if vcvarsall is None and vswhere.exists():
            try:
                result = subprocess.run(
                    [str(vswhere), "-latest", "-prerelease", "-all", "-products", "*",
                     "-property", "installationPath"],
                    capture_output=True, text=True, timeout=10
                )
                if result.returncode == 0 and result.stdout.strip():
                    for vs_line in result.stdout.strip().split('\n'):
                        vs_path = Path(vs_line.strip())
                        candidate = vs_path / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
                        if candidate.exists():
                            vcvarsall = candidate
                            break
            except Exception:
                pass

        if vcvarsall is None:
            return None

        # Run vcvarsall and capture environment
        try:
            cmd = f'"{vcvarsall}" x64 && set'
            result = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=30, shell=True
            )
            if result.returncode != 0:
                return None

            env = {}
            for line in result.stdout.split('\n'):
                if '=' in line:
                    key, _, value = line.partition('=')
                    env[key.strip()] = value.strip()

            # Cache it
            try:
                env_cache.write_text(json.dumps({
                    "timestamp": time.time(),
                    "env": env
                }))
            except Exception:
                pass

            return env

        except Exception as e:
            logger.debug(f"vcvarsall failed: {e}")
            return None

    def _get_include_paths(self) -> List[str]:
        """Build the list of include directories matching the project's CMake config."""
        paths = [
            self.rlgymcpp_src,
            self.rocketsim_src,
            self.json_include,
            self.thread_pool_include,
        ]

        if self.libtorch_include.exists():
            paths.append(self.libtorch_include)
            torch_csrc = self.libtorch_include / "torch" / "csrc" / "api" / "include"
            if torch_csrc.exists():
                paths.append(torch_csrc)

        return [str(p) for p in paths if p.exists()]

    def test_compile(self, header_code: str, class_name: str) -> Tuple[bool, List[str]]:
        """
        Test if a generated header compiles using cl.exe /Zs (syntax-only check).
        Uses vcvarsall.bat environment to get correct SDK include paths.
        Returns: (success, errors)
        """
        self._lazy_init()

        if not self._cl_exe_path:
            return False, ["cl.exe not found. Use --no-compile-test to skip."]

        # Write the generated header
        header_file = self.compile_test_dir / f"{class_name}.h"
        header_file.write_text(header_code, encoding='utf-8')

        # Create test .cpp that includes the generated header + framework
        test_cpp = self.compile_test_dir / f"test_{class_name}.cpp"
        test_code = f"""// Auto-generated compilation test for: {class_name}
#include "RLGymCPP/Rewards/Reward.h"
#include "RLGymCPP/Rewards/RewardWrapper.h"
#include "RLGymCPP/Gamestates/GameState.h"
#include "RLGymCPP/CommonValues.h"
#include "RLGymCPP/Math.h"
#include "{class_name}.h"

#include <type_traits>

static_assert(
    std::is_base_of_v<RLGC::Reward, RLGC::{class_name}>,
    "{class_name} must inherit from RLGC::Reward"
);
"""
        test_cpp.write_text(test_code, encoding='utf-8')

        # Build include path list
        include_args = []
        for path in self._get_include_paths():
            include_args.extend(["/I", path])
        # Add compile_test_dir for the generated header
        include_args.extend(["/I", str(self.compile_test_dir)])
        # Add Rewards dir so #include "Reward.h" works directly from within reward headers
        # NOTE: Do NOT add src/RLGymCPP/ directly — it contains Math.h which collides
        # with UCRT's math.h on case-insensitive Windows filesystems. Relative includes
        # like "../Math.h" from Rewards headers resolve correctly via the Rewards dir.
        rewards_dir = self.rlgymcpp_src / "RLGymCPP" / "Rewards"
        if rewards_dir.exists():
            include_args.extend(["/I", str(rewards_dir)])

        # Build the cl.exe command as a batch script that runs vcvarsall first
        # This ensures Windows SDK paths (ucrt, etc.) are correctly set
        vcvarsall = self._find_vcvarsall()

        if vcvarsall:
            # Use a batch file approach to ensure proper environment
            bat_file = self.compile_test_dir / "run_compile_test.bat"
            cl_args = " ".join(f'"{a}"' if ' ' in a else a for a in [
                "/Zs", "/EHsc", "/std:c++20", "/W3",
                "/wd4267", "/wd4244", "/DNOMINMAX",
            ] + include_args + [str(test_cpp)])

            bat_content = f"""@echo off
call "{vcvarsall}" x64 >nul 2>&1
"{self._cl_exe_path}" {cl_args}
"""
            bat_file.write_text(bat_content, encoding='utf-8')

            try:
                result = subprocess.run(
                    ["cmd", "/c", str(bat_file)],
                    capture_output=True, text=True, timeout=30,
                    cwd=str(self.compile_test_dir)
                )
                return self._parse_cl_result(result)

            except subprocess.TimeoutExpired:
                return False, ["Compilation timed out after 30 seconds"]
            except Exception as e:
                return False, [f"Compilation error: {str(e)}"]
        else:
            # Fallback: try cl.exe directly with cached env
            cmd = [
                self._cl_exe_path,
                "/Zs", "/EHsc", "/std:c++20", "/W3",
                "/wd4267", "/wd4244", "/DNOMINMAX",
            ] + include_args + [str(test_cpp)]

            try:
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=30, cwd=str(self.compile_test_dir),
                    env=self._msvc_env
                )
                return self._parse_cl_result(result)

            except subprocess.TimeoutExpired:
                return False, ["Compilation timed out after 30 seconds"]
            except Exception as e:
                return False, [f"Compilation error: {str(e)}"]

    def _find_vcvarsall(self) -> Optional[str]:
        """Find vcvarsall.bat corresponding to the cl.exe we're using."""
        if self._cl_exe_path:
            cl_path = Path(self._cl_exe_path)
            for parent in cl_path.parents:
                if parent.name == "VC":
                    candidate = parent / "Auxiliary" / "Build" / "vcvarsall.bat"
                    if candidate.exists():
                        return str(candidate)
        return None

    def _parse_cl_result(self, result: subprocess.CompletedProcess) -> Tuple[bool, List[str]]:
        """Parse cl.exe output and extract errors."""
        errors = []
        if result.returncode != 0:
            output = (result.stdout or "") + "\n" + (result.stderr or "")
            for line in output.split('\n'):
                line = line.strip()
                if not line:
                    continue
                if line.startswith("Microsoft (R)") or line.startswith("Copyright (C)"):
                    continue
                if line.endswith('.cpp') and 'error' not in line.lower():
                    continue
                if ': error ' in line or ': fatal error ' in line:
                    line = line.replace(str(self.compile_test_dir) + '\\', '')
                    errors.append(line)
            if not errors:
                fallback = [l.strip() for l in output.split('\n')
                            if l.strip() and 'error' in l.lower()
                            and not l.strip().startswith("Microsoft")
                            and not l.strip().startswith("Copyright")]
                if fallback:
                    errors = fallback[:5]
        return result.returncode == 0, errors

    def is_available(self) -> bool:
        """Check if compilation testing is available."""
        self._lazy_init()
        return self._cl_exe_path is not None


# ============================================================================
# COST TRACKER
# ============================================================================

class CostTracker:
    """Track Claude API costs to enforce budget limits."""

    # Approximate costs per model (USD per 1M tokens)
    MODEL_COSTS = {
        "claude-sonnet-4-20250514": {"input": 3.0, "output": 15.0},
        "claude-opus-4-5-20250514": {"input": 15.0, "output": 75.0},
    }

    def __init__(self, cost_log_file: Path = COST_LOG_FILE):
        self.cost_log_file = cost_log_file
        self.cost_log_file.parent.mkdir(parents=True, exist_ok=True)

    def estimate_cost(self, model: str, input_tokens: int, output_tokens: int) -> float:
        costs = self.MODEL_COSTS.get(model, {"input": 15.0, "output": 75.0})
        return (input_tokens * costs["input"] + output_tokens * costs["output"]) / 1_000_000

    def record_call(self, model: str, input_tokens: int, output_tokens: int) -> float:
        cost = self.estimate_cost(model, input_tokens, output_tokens)
        entry = {
            "timestamp": datetime.utcnow().isoformat(),
            "model": model,
            "input_tokens": input_tokens,
            "output_tokens": output_tokens,
            "cost_usd": round(cost, 6),
        }

        log = self._load_log()
        log.append(entry)
        try:
            self.cost_log_file.write_text(json.dumps(log, indent=2))
        except Exception:
            pass
        return cost

    def get_daily_spend(self) -> float:
        today = datetime.utcnow().date().isoformat()
        log = self._load_log()
        return sum(e["cost_usd"] for e in log if e["timestamp"].startswith(today))

    def check_budget(self, max_daily: float = MAX_DAILY_COST_USD) -> bool:
        return self.get_daily_spend() < max_daily

    def _load_log(self) -> List[Dict]:
        if self.cost_log_file.exists():
            try:
                return json.loads(self.cost_log_file.read_text())
            except Exception:
                pass
        return []


# ============================================================================
# LLM REWARD GENERATOR
# ============================================================================

class LLMRewardGenerator:
    """Generates and fixes C++ reward code via Claude API."""

    def __init__(self, model: str = DEFAULT_MODEL, api_key: str = None):
        try:
            import anthropic
        except ImportError:
            raise ImportError(
                "anthropic package not installed. Run: pip install anthropic"
            )

        self.api_key = api_key or os.getenv("ANTHROPIC_API_KEY")
        if not self.api_key:
            raise ValueError(
                "Set ANTHROPIC_API_KEY environment variable or pass api_key parameter"
            )
        self.client = anthropic.Anthropic(api_key=self.api_key)
        self.model = model
        self.cost_tracker = CostTracker()
        self.source_finder = RewardSourceFinder()

    def generate_fix(self, request: GenerationRequest) -> Tuple[str, float]:
        """Generate a fix for a broken reward. Returns (code, cost_usd)."""
        prompt = self._build_fix_prompt(request)
        return self._call_api(prompt, MAX_TOKENS_FIX)

    def generate_new(self, request: GenerationRequest) -> Tuple[str, float]:
        """Generate a new reward. Returns (code, cost_usd)."""
        prompt = self._build_new_prompt(request)
        return self._call_api(prompt, MAX_TOKENS_NEW)

    def generate_fix_from_errors(self, code: str, errors: List[str],
                                  request: GenerationRequest) -> Tuple[str, float]:
        """Re-generate code based on compiler errors."""
        prompt = self._build_error_fix_prompt(code, errors, request)
        return self._call_api(prompt, MAX_TOKENS_FIX)

    def _call_api(self, prompt: str, max_tokens: int) -> Tuple[str, float]:
        """Call Claude API and return (extracted_code, cost_usd)."""
        if not self.cost_tracker.check_budget():
            raise RuntimeError(
                f"Daily API budget exceeded (${MAX_DAILY_COST_USD:.2f}). "
                f"Today's spend: ${self.cost_tracker.get_daily_spend():.4f}"
            )

        message = self.client.messages.create(
            model=self.model,
            max_tokens=max_tokens,
            messages=[{"role": "user", "content": prompt}]
        )

        raw_text = message.content[0].text
        code = self._extract_cpp_code(raw_text)

        cost = self.cost_tracker.record_call(
            self.model,
            message.usage.input_tokens,
            message.usage.output_tokens
        )

        return code, cost

    # ---- Prompt builders ----

    def _build_fix_prompt(self, req: GenerationRequest) -> str:
        """Build the prompt for fixing a broken reward."""

        # Get exemplar rewards for context
        exemplars = self.source_finder.get_exemplar_rewards("simple")
        exemplar_text = ""
        for name, code in exemplars[:2]:
            # Truncate long exemplars
            lines = code.split('\n')[:80]
            exemplar_text += f"\n// ===== {name} (reference example) =====\n"
            exemplar_text += '\n'.join(lines) + "\n"

        metrics_text = ""
        if req.metrics:
            metrics_text = f"""
CURRENT METRICS:
- Activation Rate: {req.metrics.get('activation_rate', 0)*100:.1f}%
- Mean Value: {req.metrics.get('mean_value', 0):.6f}
- Variance: {req.metrics.get('variance', 0):.6f}
- Weight: {req.metrics.get('weight', 0):.1f}
- Sample Count: {req.metrics.get('sample_count', 0)}
"""

        return f"""You are a C++ reward function engineer for a Rocket League AI training system (RLGymCPP).
Fix the broken reward function described below.

PROBLEM:
- Reward Name: {req.reward_name}
- Problem Type: {req.problem_type}
- Severity: {req.severity:.2f}/1.0
- Explanation: {req.explanation}
{metrics_text}
SUGGESTED FIXES:
{chr(10).join(f"  - {fix}" for fix in req.suggested_fixes)}

CURRENT IMPLEMENTATION:
```cpp
{req.existing_code or "// Source code not found for this reward"}
```

REFERENCE IMPLEMENTATIONS (working examples from this codebase):
```cpp
{exemplar_text}
```

FRAMEWORK REQUIREMENTS:
1. Header-only file with #pragma once
2. Everything inside namespace RLGC {{}}
3. Class must inherit from RLGC::Reward (from "Reward.h")
4. Override: virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override
5. Optionally override: virtual void Reset(const GameState& initialState) override
6. Optionally override: virtual void PreStep(const GameState& state) override
7. C++20 standard, no external dependencies beyond the framework
8. Available Player fields: pos (Vec), vel (Vec), rotMat (RotMat with forward/right/up), boost (float 0-100),
   isOnGround (bool), isFlipping (bool), prev (pointer to previous frame), team (int), carId (uint32_t),
   ballTouchedStep (int), eventState (goal/assist/shot/save/bump/demo bools), prevAction, index (int)
9. Available GameState fields: players (vector<Player>), ball.pos (Vec), ball.vel (Vec), ball.angVel (Vec),
   prev (pointer to previous GameState), deltaTime (float), goalScored (bool), boostPads (array)
10. Available: Vec type with .Dot(), .Length(), .Normalized(), .Cross(), arithmetic operators (+,-,*,/)
11. Available: CommonValues:: (CAR_MAX_SPEED, BALL_MAX_SPEED, SIDE_WALL_X, BACK_WALL_Y, CEILING_Z,
    ORANGE_GOAL_CENTER, BLUE_GOAL_CENTER, BALL_RADIUS, etc.)
12. Available: RS_CLAMP(val, min, max), RS_MIN(a,b), RS_MAX(a,b) macros
13. Per-player state tracking: use mutable std::map<uint32_t, YourStruct> keyed by player.carId
14. Access previous frame: player.prev->... and state.prev->... (check for nullptr first!)
15. Return values: typically 0.0 to 1.0 (can be negative for penalties, can exceed 1.0)
16. Must be computationally cheap (< 1ms per call per agent)
17. Always check state.prev != nullptr before accessing previous-frame data
18. Use #include "Reward.h" and any standard library headers you need (<map>, <cmath>, <algorithm>)
19. Include "../Math.h" if you need RLGC::Math functions

Return ONLY the complete C++ header file. Include all necessary #include directives.
Do NOT include any explanation, markdown fences, or comments outside the code.
Start with #pragma once."""

    def _build_new_prompt(self, req: GenerationRequest) -> str:
        """Build prompt for creating a new reward."""
        exemplars = self.source_finder.get_exemplar_rewards("stateful")
        exemplar_text = ""
        for name, code in exemplars[:1]:
            lines = code.split('\n')[:100]
            exemplar_text += f"\n// ===== {name} (reference example) =====\n"
            exemplar_text += '\n'.join(lines) + "\n"

        return f"""You are a C++ reward function engineer for a Rocket League AI training system (RLGymCPP).
Create a new reward function for the behavior described below.

DESIRED BEHAVIOR: {req.explanation}

SIMILAR EXISTING REWARDS (avoid duplication):
{chr(10).join(f"  - {r}" for r in req.similar_rewards[:5])}

REFERENCE IMPLEMENTATION (working example from this codebase):
```cpp
{exemplar_text}
```

FRAMEWORK REQUIREMENTS:
1. Header-only file with #pragma once
2. Everything inside namespace RLGC {{}}
3. Class must inherit from RLGC::Reward (from "Reward.h")
4. Override: virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override
5. Optionally override: virtual void Reset(const GameState& initialState) override
6. C++20 standard, no external dependencies beyond the framework
7. Available Player fields: pos, vel, rotMat (forward/right/up), boost, isOnGround, isFlipping,
   prev, team, carId, ballTouchedStep, eventState, index
8. Available GameState fields: players, ball (pos/vel/angVel), prev, deltaTime, goalScored, boostPads
9. Available: Vec (.Dot(), .Length(), .Normalized(), .Cross(), arithmetic operators)
10. Available: CommonValues:: constants, RS_CLAMP/RS_MIN/RS_MAX macros
11. Per-player state: mutable std::map<uint32_t, YourStruct> keyed by player.carId
12. Always check state.prev != nullptr before accessing previous-frame data

REWARD DESIGN GUIDELINES:
- Smooth gradient (avoid binary on/off rewards)
- Scale output to approximately 0.0-1.0 range
- Handle edge cases (missing prev pointer, empty players list)
- Account for 3v3 multi-agent gameplay
- Use per-player state tracking with std::map<uint32_t, ...> if needed

Return ONLY the complete C++ header file with all includes. No explanation, no markdown.
Start with #pragma once."""

    def _build_error_fix_prompt(self, code: str, errors: List[str],
                                 req: GenerationRequest) -> str:
        """Build prompt for fixing compilation errors."""
        errors_text = '\n'.join(errors[:15])

        return f"""The following C++ reward function for RLGymCPP failed to compile. Fix the compilation errors.

COMPILER ERRORS:
{errors_text}

BROKEN CODE:
```cpp
{code}
```

FRAMEWORK CONTEXT:
- namespace RLGC, inherit from Reward, override GetReward(const Player&, const GameState&, bool)
- Available: Player (pos, vel, rotMat, boost, isOnGround, prev, carId, team, eventState, index)
- Available: GameState (players, ball.pos, ball.vel, prev, deltaTime, goalScored)
- Available: Vec (.Dot(), .Length(), .Normalized(), arithmetic), RS_CLAMP, RS_MIN, RS_MAX
- Available: CommonValues:: constants
- Check state.prev != nullptr before use
- C++20 standard, header-only with #pragma once

Fix ONLY the compilation errors. Do not change the reward logic unless it caused the error.
Return ONLY the fixed C++ header file. No explanation, no markdown.
Start with #pragma once."""

    # ---- Code extraction ----

    @staticmethod
    def _extract_cpp_code(text: str) -> str:
        """Extract C++ code from LLM response, stripping markdown fences."""
        # Try ```cpp ... ``` first
        match = re.search(r'```(?:cpp|c\+\+)?\s*\n(.*?)```', text, re.DOTALL)
        if match:
            return match.group(1).strip()

        # Try ``` ... ```
        match = re.search(r'```\s*\n(.*?)```', text, re.DOTALL)
        if match:
            return match.group(1).strip()

        # If no fences, find the first line that looks like C++ code
        lines = text.strip().split('\n')
        start = 0
        for i, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith('#pragma') or stripped.startswith('#include') or \
               stripped.startswith('namespace') or stripped.startswith('//'):
                start = i
                break

        return '\n'.join(lines[start:]).strip()

    @staticmethod
    def extract_class_name(code: str) -> str:
        """Extract the main reward class name from generated code."""
        # Try class XReward : public Reward
        match = re.search(
            r'class\s+(\w+)\s*:\s*public\s+(?:RLGC::)?Reward\b',
            code
        )
        if match:
            return match.group(1)

        # Try struct XReward : public Reward
        match = re.search(
            r'struct\s+(\w+)\s*:\s*public\s+(?:RLGC::)?Reward\b',
            code
        )
        if match:
            return match.group(1)

        return "UnknownReward"


# ============================================================================
# PROMPT FORMATTER (generates human-readable markdown prompts)
# ============================================================================

class PromptFormatter:
    """Formats reward diagnostics into human-readable copy/paste prompts."""

    def __init__(self, framework_doc: str = ""):
        """framework_doc: Optional pre-rendered framework requirements text."""
        self.framework_doc = framework_doc

    def format_fix_prompt(self, request: GenerationRequest) -> str:
        """Generate a human-readable prompt for fixing a broken reward."""
        exemplars = ""
        if request.similar_rewards:
            exemplars = f"\n## Similar Working Rewards (for reference)\n{request.similar_rewards}"

        metrics_str = ""
        if request.metrics:
            m = request.metrics
            metrics_str = f"""
  - Activation Rate: {m.get('activation_rate', 'N/A')}
  - Mean Value: {m.get('mean_value', 'N/A')}
  - Variance: {m.get('variance', 'N/A')}
  - Weight: {m.get('weight', 'N/A')}
  - Sample Count: {m.get('sample_count', 'N/A')}"""

        framework_section = ""
        if self.framework_doc:
            framework_section = f"\n## Framework Requirements\nSee attached framework_requirements.txt for:\n  - Reward API details\n  - Available types and macros\n  - Patterns to follow/avoid\n\n"

        prompt = f"""# Fix Reward: {request.reward_name}

## Problem Diagnosis
- **Type**: {request.problem_type} (severity: {request.severity:.2f}/1.0)
- **Explanation**: {request.explanation}
- **Metrics**:{metrics_str}

## Suggested Fixes
{chr(10).join(f"- {fix}" for fix in request.suggested_fixes)}

## Current Source Code
```cpp
{request.existing_code or "(Source code not found)"}
```
{exemplars}
{framework_section}
## Task
Generate a fixed C++ header for `{request.reward_name}` that addresses the problem above.

**Requirements:**
1. Fix the specific issue identified in the Problem section
2. Return ONLY the complete #pragma once ... closing brace code block
3. Do NOT include explanations or markdown formatting
4. Preserve the class name: `{request.reward_name}`
5. Must still inherit from RLGC::Reward
6. Must implement GetReward(const Player&, const GameState&, bool) override
"""
        return prompt

    def format_new_prompt(self, request: GenerationRequest) -> str:
        """Generate a prompt for creating a new reward."""
        exemplars = ""
        if request.similar_rewards:
            exemplars = f"\n## Similar Rewards (for reference)\n{request.similar_rewards}"

        framework_section = ""
        if self.framework_doc:
            framework_section = f"\n## Framework Requirements\nSee attached framework_requirements.txt.\n"

        prompt = f"""# Create New Reward: {request.reward_name}

## Description
{request.explanation}

## Metrics Target
- Expected activation rate: ~0.1-0.5 (10-50% of steps)
- Mean value range: -1.0 to +1.0
- Should support per-player state if needed
{exemplars}
{framework_section}
## Task
Generate a new C++ header for `{request.reward_name}` that implements this reward.

**Requirements:**
1. Return ONLY the complete #pragma once ... closing brace code block
2. Class name: `{request.reward_name}` inheriting from `RLGC::Reward`
3. Implement GetReward(const Player&, const GameState&, bool) override
4. Follow C++20 standards used in the project
5. No file I/O, system calls, or external dependencies
"""
        return prompt


# ============================================================================
# FRAMEWORK REQUIREMENTS GENERATOR
# ============================================================================

class FrameworkRequirementsDoc:
    """Generates static framework requirements documentation."""

    @staticmethod
    def generate() -> str:
        """Generate framework requirements text."""
        return """# C++ Reward Framework Requirements

## Reward Base Class API

```cpp
class Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) = 0;
    virtual void Reset(const GameState& initialState) {}
    virtual void PreStep(const GameState& state) {}
    virtual std::string GetName() const;
};
```

## Key Types

### Vec
3D vector with methods:
- `float Length()` — magnitude
- `float DistanceTo(const Vec& other)` — Euclidean distance
- `Vec Normalized()` — unit vector
- Operators: `+`, `-`, `*` (scalar), `/`, `==`, `!=`

### GameState
Represents current game state:
- `Ball ball` — ball position, velocity
- `std::vector<Player> players` — all players
- `std::vector<Boost> boosts` — boost pad locations
- `bool ball_hit_by_player` — was ball hit this frame?

### Player
Represents a player:
- `uint32_t carId` — unique car ID
- `Vec pos` — position
- `Vec vel` — velocity
- `Angle rot` — rotation (pitch, yaw, roll)
- `float boost` — boost amount (0-100)
- `bool on_ground` — contact with arena floor?
- `bool supersonic` — supersonic state?
- `PlayerData prev` — previous frame state (for history)

### CommonValues
Helpful constants:
```cpp
namespace CommonValues {
    constexpr float BALL_MAX_SPEED = 2300.0f;
    constexpr float CAR_MAX_SPEED = 2300.0f;
    constexpr float SUPERSONIC_SPEED = 2300.0f;
    constexpr float GRAVITY = 650.0f;
    constexpr float ARENA_LENGTH = 10240.0f;
    constexpr float ARENA_WIDTH = 8240.0f;
}
```

### Useful Macros
```cpp
RS_CLAMP(x, min, max)    // Clamp x to [min, max]
RS_MIN(a, b)              // Minimum
RS_MAX(a, b)              // Maximum
RG_LOG(msg)               // Debug log
RG_ERR_CLOSE(msg)         // Error log
```

## Pattern Examples

### Simple Reward
```cpp
#pragma once
#include "Reward.h"

namespace RLGC {
    class SimpleReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float reward = /* computation */;
            return RS_CLAMP(reward, -1.0f, 1.0f);
        }
    };
}
```

### Stateful Reward (tracking per-player history)
```cpp
#pragma once
#include "Reward.h"
#include <map>

namespace RLGC {
    class StatefulReward : public Reward {
    private:
        mutable std::map<uint32_t, float> prev_values;  // Keyed by carId

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float current = /* computation */;
            float prev = prev_values[player.carId];
            prev_values[player.carId] = current;  // Update for next frame
            return RS_CLAMP(current - prev, -1.0f, 1.0f);
        }
    };
}
```

## Dangerous Patterns (AVOID)

❌ File I/O: `std::ofstream`, `fopen`, `std::filesystem::write`
❌ System calls: `system()`, `std::system`
❌ Thread creation: `std::thread`, `pthread`
❌ Infinite loops: `while(true)` without breaks
❌ Unbounded allocation: `new` without `delete`
❌ Division by zero: `x / y` without checking y != 0
❌ sqrt of negative: `std::sqrt(x)` without checking x >= 0
❌ Global state: static variables (except constants)
❌ Random numbers without seeding

## Compilation Target

- **Language**: C++20
- **Compiler**: MSVC 19.44 (Visual Studio 2022)
- **Architecture**: x64
- **Optimization**: /O2 (Release build)
"""


# ============================================================================
# ORCHESTRATOR
# ============================================================================

class RewardGenerationOrchestrator:
    """Main orchestrator for Phase 3-4 reward generation pipeline."""

    def __init__(self, args):
        self.checkpoints_root = Path(args.checkpoints_root or DEFAULT_CHECKPOINTS_ROOT)
        self.checkpoint_path = Path(args.checkpoint) if args.checkpoint else None
        self.reward_mode = args.mode  # "fix", "new", "both"
        self.max_fixes = args.max_fixes
        self.dry_run = args.dry_run
        self.no_compile = args.no_compile_test
        self.model = args.model or DEFAULT_MODEL

        # New mode parameters
        self.generation_mode = getattr(args, 'generation_mode', 'prompt-export')  # "prompt-export" or "api"
        self.validate_code_file = getattr(args, 'validate_code', None)  # For --validate-code subcommand

        self.llm: Optional[LLMRewardGenerator] = None  # Lazy init
        self.compiler = CompilationTester()
        self.source_finder = RewardSourceFinder()

        GENERATED_DIR.mkdir(parents=True, exist_ok=True)
        STAGING_DIR.mkdir(parents=True, exist_ok=True)
        PROMPTS_DIR = GENERATED_DIR / "prompts"
        PROMPTS_DIR.mkdir(parents=True, exist_ok=True)

    def run(self) -> GenerationManifest:
        """Run the full generation pipeline (dispatch based on generation_mode)."""
        # Handle --validate-code subcommand
        if self.validate_code_file:
            return self._run_validation()

        # Branch by generation mode
        if self.generation_mode == "prompt-export":
            return self._run_prompt_export()
        elif self.generation_mode == "api":
            return self._run_api_generation()
        else:
            raise ValueError(f"Unknown generation mode: {self.generation_mode}")

    def _run_validation(self) -> GenerationManifest:
        """Validate a user-provided reward code file."""
        file_path = Path(self.validate_code_file)
        if not file_path.exists():
            raise FileNotFoundError(f"File not found: {file_path}")

        logger.info(f"Validating: {file_path}")

        try:
            code = file_path.read_text(encoding='utf-8')
        except Exception as e:
            raise IOError(f"Failed to read {file_path}: {e}")

        # Extract class name
        class_name = LLMRewardGenerator.extract_class_name(code)
        logger.info(f"Extracted class name: {class_name}")

        manifest = GenerationManifest(
            checkpoint_path="(validation)",
            total_timesteps=0,
            run_timestamp=datetime.utcnow().isoformat(),
        )

        # Run static validation
        validator = CodeValidator()
        validation_result = validator.validate(code)
        if not validation_result[0]:
            logger.error("Static validation failed:")
            for error in validation_result[1]:
                logger.error(f"  - {error}")
            errors = validation_result[1]
        else:
            errors = []

        # Run compilation test if not skipped
        compiled = True
        compile_errors = []
        if not self.no_compile and self.compiler.is_available():
            compiled, compile_errors = self.compiler.test_compile(code, class_name)
            if not compiled:
                logger.error("Compilation test failed:")
                for error in compile_errors:
                    logger.error(f"  {error}")

        # Prepare result
        result = GenerationResult(
            reward_name=class_name,
            class_name=class_name,
            mode="validation",
            success=(not errors) and (compiled if not self.no_compile else True),
            generated_code=code,
            compiled=compiled,
            compile_errors=compile_errors + errors,
            attempts=1,
            api_cost_usd=0.0,
            timestamp=datetime.utcnow().isoformat(),
            output_file=str(file_path),
        )

        manifest.results.append(result)

        # Print result
        if result.success:
            logger.info("[OK] Validation passed - code is ready for deployment")
            print(f"\n{'='*70}")
            print(f"[OK] VALIDATION PASSED")
            print(f"  File: {file_path}")
            print(f"  Class: {class_name}")
            print(f"  Ready for deployment to RLGymCPP/src/RLGymCPP/Rewards/")
            print(f"{'='*70}\n")
        else:
            logger.error("[FAIL] Validation failed - fix errors and retry")
            print(f"\n{'='*70}")
            print(f"[FAIL] VALIDATION FAILED")
            print(f"  File: {file_path}")
            print(f"  Errors: {len(result.compile_errors)}")
            for err in result.compile_errors[:5]:
                print(f"    - {err}")
            if len(result.compile_errors) > 5:
                print(f"    ... and {len(result.compile_errors) - 5} more")
            print(f"{'='*70}\n")

        return manifest

    def _run_prompt_export(self) -> GenerationManifest:
        """Generate human-readable prompts for copy/paste into Claude Code."""
        # 1. Find target checkpoint
        checkpoint = self._resolve_checkpoint()
        logger.info(f"Using checkpoint: {checkpoint}")

        # 2. Load diagnostics and metrics
        diag_path = checkpoint / "REWARD_DIAGNOSIS.json"
        metrics_path = checkpoint / "REWARD_METRICS.json"

        if not diag_path.exists():
            raise FileNotFoundError(f"No REWARD_DIAGNOSIS.json in {checkpoint}")
        if not metrics_path.exists():
            raise FileNotFoundError(f"No REWARD_METRICS.json in {checkpoint}")

        diagnosis = json.loads(diag_path.read_text())
        metrics = json.loads(metrics_path.read_text())

        total_timesteps = metrics.get("total_timesteps", 0)
        logger.info(f"Checkpoint timesteps: {total_timesteps:,}")

        manifest = GenerationManifest(
            checkpoint_path=str(checkpoint),
            total_timesteps=total_timesteps,
            run_timestamp=datetime.utcnow().isoformat(),
        )

        # 3. Build generation requests
        requests = self._build_requests(diagnosis, metrics)

        if not requests:
            logger.info("No actionable rewards found. Nothing to do.")
            return manifest

        logger.info(f"Found {len(requests)} actionable rewards (processing up to {self.max_fixes})")

        # 4. Dry run mode
        if self.dry_run:
            print(f"\n{'='*70}")
            print(f"DRY RUN — Would create these prompts:")
            print(f"{'='*70}")
            for i, req in enumerate(requests[:self.max_fixes]):
                print(f"  {i+1}. [{req.problem_type}] {req.reward_name} "
                      f"(severity {req.severity:.2f})")
                print(f"     {req.explanation}")
            remaining = len(requests) - self.max_fixes
            if remaining > 0:
                print(f"  ... and {remaining} more (increase --max-fixes to process)")
            print(f"{'='*70}\n")
            return manifest

        # 5. Generate framework requirements doc (once)
        framework_doc = FrameworkRequirementsDoc.generate()
        framework_path = GENERATED_DIR / "framework_requirements.txt"
        if not framework_path.exists():
            framework_path.write_text(framework_doc, encoding='utf-8')
            logger.info(f"Created framework_requirements.txt")

        # 6. Generate and export prompts
        formatter = PromptFormatter(framework_doc=framework_doc)
        prompts_dir = GENERATED_DIR / "prompts"
        prompts_dir.mkdir(parents=True, exist_ok=True)

        for i, req in enumerate(requests[:self.max_fixes], 1):
            if req.mode == "fix":
                prompt = formatter.format_fix_prompt(req)
            elif req.mode == "new":
                prompt = formatter.format_new_prompt(req)
            else:
                continue

            # Save prompt to file
            sanitized_name = "".join(c if c.isalnum() or c in "_-" else "_" for c in req.reward_name)
            prompt_file = prompts_dir / f"Prompt_{i:03d}_{sanitized_name}.txt"
            prompt_file.write_text(prompt, encoding='utf-8')
            logger.info(f"Saved prompt: {prompt_file.name}")

            # Record in manifest
            result = GenerationResult(
                reward_name=req.reward_name,
                class_name=req.reward_name,
                mode=req.mode,
                success=True,
                generated_code=prompt,
                compiled=False,  # No compilation for prompts
                compile_errors=[],
                attempts=1,
                api_cost_usd=0.0,
                timestamp=datetime.utcnow().isoformat(),
                output_file=str(prompt_file),
            )
            manifest.results.append(result)

        # Print summary
        print(f"\n{'='*70}")
        print(f"[OK] PROMPTS EXPORTED ({len(manifest.results)} files)")
        print(f"  Location: {prompts_dir}")
        print(f"{'='*70}")
        for r in manifest.results:
            print(f"  Prompt_{r.output_file.split('_')[-2]}_...")
        print(f"\n  Next steps:")
        print(f"  1. Open prompts/ directory")
        print(f"  2. Copy entire prompt from Prompt_NNN_*.txt file")
        print(f"  3. Paste into Claude Code chat")
        print(f"  4. Copy Claude's generated code")
        print(f"  5. Run: python reward_llm_generator.py --validate-code <file>")
        print(f"{'='*70}\n")

        return manifest

    def _run_api_generation(self) -> GenerationManifest:
        """Original API-based generation with automatic compilation testing."""
        # 1. Find target checkpoint
        checkpoint = self._resolve_checkpoint()
        logger.info(f"Using checkpoint: {checkpoint}")

        # 2. Load diagnostics and metrics
        diag_path = checkpoint / "REWARD_DIAGNOSIS.json"
        metrics_path = checkpoint / "REWARD_METRICS.json"

        if not diag_path.exists():
            raise FileNotFoundError(f"No REWARD_DIAGNOSIS.json in {checkpoint}")
        if not metrics_path.exists():
            raise FileNotFoundError(f"No REWARD_METRICS.json in {checkpoint}")

        diagnosis = json.loads(diag_path.read_text())
        metrics = json.loads(metrics_path.read_text())

        total_timesteps = metrics.get("total_timesteps", 0)
        logger.info(f"Checkpoint timesteps: {total_timesteps:,}")
        logger.info(f"Reward count: {metrics.get('reward_count', '?')}")
        logger.info(f"Healthy: {diagnosis.get('healthy_reward_count', '?')}, "
                     f"Problematic: {diagnosis.get('problematic_reward_count', '?')}")

        manifest = GenerationManifest(
            checkpoint_path=str(checkpoint),
            total_timesteps=total_timesteps,
            run_timestamp=datetime.utcnow().isoformat(),
        )

        # 3. Build generation requests
        requests = self._build_requests(diagnosis, metrics)

        if not requests:
            logger.info("No actionable rewards found. Nothing to do.")
            return manifest

        logger.info(f"Found {len(requests)} actionable rewards (processing up to {self.max_fixes})")

        # 4. Dry run mode
        if self.dry_run:
            print(f"\n{'='*70}")
            print(f"DRY RUN — Would process these rewards:")
            print(f"{'='*70}")
            for i, req in enumerate(requests[:self.max_fixes]):
                source_found = "source found" if req.existing_code else "NO SOURCE"
                print(f"  {i+1}. [{req.problem_type}] {req.reward_name} "
                      f"(severity {req.severity:.2f}) — {source_found}")
                print(f"     {req.explanation}")
            remaining = len(requests) - self.max_fixes
            if remaining > 0:
                print(f"  ... and {remaining} more (increase --max-fixes to process)")
            print(f"{'='*70}\n")
            return manifest

        # 5. Check compilation tester availability
        if not self.no_compile and not self.compiler.is_available():
            logger.warning("Compilation testing unavailable (cl.exe not found). "
                          "Use --no-compile-test to skip.")
            logger.warning("Continuing without compilation testing...")
            self.no_compile = True

        # 6. Initialize LLM
        self.llm = LLMRewardGenerator(model=self.model)
        logger.info(f"Using model: {self.model}")

        # 7. Process each request (API mode only)
        for i, req in enumerate(requests[:self.max_fixes]):
            logger.info(f"\n[{i+1}/{min(len(requests), self.max_fixes)}] "
                       f"Processing {req.reward_name} ({req.mode}: {req.problem_type})...")

            result = self._process_request(req)
            manifest.results.append(result)
            manifest.total_api_cost_usd += result.api_cost_usd

            status = "SUCCESS" if result.success else "FAILED"
            logger.info(f"  -> {status} (compiled={result.compiled}, "
                       f"attempts={result.attempts}, cost=${result.api_cost_usd:.4f})")

        # 8. Write manifest
        manifest_path = STAGING_DIR / f"manifest_{total_timesteps}.json"
        manifest_path.write_text(json.dumps(asdict(manifest), indent=2))
        logger.info(f"\nManifest written to {manifest_path}")

        return manifest

    def _resolve_checkpoint(self) -> Path:
        """Find the checkpoint to analyze."""
        if self.checkpoint_path:
            if not self.checkpoint_path.exists():
                raise FileNotFoundError(f"Checkpoint not found: {self.checkpoint_path}")
            return self.checkpoint_path

        # Find latest checkpoint with diagnostics
        if not self.checkpoints_root.exists():
            raise FileNotFoundError(f"Checkpoints root not found: {self.checkpoints_root}")

        candidates = []
        for entry in self.checkpoints_root.iterdir():
            if entry.is_dir() and entry.name.isdigit():
                if (entry / "REWARD_DIAGNOSIS.json").exists():
                    candidates.append((int(entry.name), entry))

        if not candidates:
            raise FileNotFoundError(
                f"No checkpoints with REWARD_DIAGNOSIS.json in {self.checkpoints_root}"
            )

        candidates.sort(reverse=True)
        return candidates[0][1]

    def _build_requests(self, diagnosis: Dict, metrics: Dict) -> List[GenerationRequest]:
        """Build generation requests from diagnosis data."""
        requests = []
        rewards_data = metrics.get("rewards", {})

        for diag in diagnosis.get("diagnoses", []):
            problem_type = diag.get("problem_type", "UNKNOWN")
            severity = diag.get("severity", 0)
            reward_name = diag.get("reward_name", "")

            # Skip low-severity issues
            if severity < 0.3:
                continue

            # Skip gradient dilution (not fixable by code changes)
            if problem_type == "GRADIENT_DILUTION":
                continue

            # Skip template rewards (can't modify)
            if '<' in reward_name or '>' in reward_name:
                continue

            # Only process fix requests in fix/both mode
            if self.reward_mode not in ("fix", "both"):
                continue

            # Find existing source code
            source = self.source_finder.find_source(reward_name)
            existing_code = source[1] if source else None

            # Get metrics for this reward
            reward_metrics = rewards_data.get(reward_name, {})

            requests.append(GenerationRequest(
                reward_name=reward_name,
                mode="fix",
                problem_type=problem_type,
                severity=severity,
                explanation=diag.get("explanation", ""),
                suggested_fixes=diag.get("suggested_fixes", []),
                existing_code=existing_code,
                metrics=reward_metrics,
                similar_rewards=self._find_similar_rewards(reward_name, rewards_data),
            ))

        # Sort by severity (highest first)
        requests.sort(key=lambda r: r.severity, reverse=True)
        return requests

    def _find_similar_rewards(self, name: str, rewards_data: Dict) -> List[str]:
        """Find rewards with similar names/categories that are healthy."""
        similar = []

        # CamelCase split for keyword matching
        keywords = [
            part.lower()
            for part in re.split(r'(?=[A-Z])', name)
            if len(part) > 2
        ]

        for other_name, other_metrics in rewards_data.items():
            if other_name == name:
                continue
            other_lower = other_name.lower()
            if any(kw in other_lower for kw in keywords):
                if other_metrics.get("activation_rate", 0) > 0.05:
                    similar.append(other_name)

        return similar[:5]

    def _process_request(self, req: GenerationRequest) -> GenerationResult:
        """Process a single generation request with retry logic."""
        total_cost = 0.0
        attempts = 0
        last_code = ""
        last_errors: List[str] = []

        for attempt in range(MAX_COMPILE_RETRIES + 1):
            attempts += 1

            try:
                # Generate code
                if attempt == 0:
                    if req.mode == "fix":
                        code, cost = self.llm.generate_fix(req)
                    else:
                        code, cost = self.llm.generate_new(req)
                else:
                    # Retry with error context
                    logger.info(f"  Retry {attempt}/{MAX_COMPILE_RETRIES} "
                              f"with compiler error feedback...")
                    code, cost = self.llm.generate_fix_from_errors(
                        last_code, last_errors, req
                    )

                total_cost += cost
                last_code = code

                # Extract class name
                class_name = LLMRewardGenerator.extract_class_name(code)
                logger.info(f"  Generated class: {class_name} "
                           f"({len(code)} chars, ${cost:.4f})")

                # Static validation
                valid, issues = CodeValidator.validate(code)
                if issues:
                    for issue in issues:
                        logger.info(f"  Validation: {issue}")
                if not valid:
                    last_errors = [i for i in issues if not i.startswith("WARNING:")]
                    continue

                # Compilation test
                if self.no_compile:
                    compiled = True
                    compile_errors: List[str] = []
                    logger.info(f"  Compilation test: SKIPPED (--no-compile-test)")
                else:
                    logger.info(f"  Compiling...")
                    compiled, compile_errors = self.compiler.test_compile(code, class_name)
                    if compiled:
                        logger.info(f"  Compilation: PASSED")
                    else:
                        logger.info(f"  Compilation: FAILED")
                        for err in compile_errors[:3]:
                            logger.info(f"    {err}")

                if compiled:
                    # Save to staging
                    output_file = STAGING_DIR / f"{class_name}.h"
                    output_file.write_text(code, encoding='utf-8')

                    return GenerationResult(
                        reward_name=req.reward_name,
                        class_name=class_name,
                        mode=req.mode,
                        success=True,
                        generated_code=code,
                        compiled=True,
                        compile_errors=[],
                        attempts=attempts,
                        api_cost_usd=total_cost,
                        timestamp=datetime.utcnow().isoformat(),
                        output_file=str(output_file),
                    )
                else:
                    last_errors = compile_errors

            except RuntimeError as e:
                # Budget exceeded
                logger.error(f"  {e}")
                return GenerationResult(
                    reward_name=req.reward_name,
                    class_name="Unknown",
                    mode=req.mode,
                    success=False,
                    generated_code=last_code,
                    compiled=False,
                    compile_errors=[str(e)],
                    attempts=attempts,
                    api_cost_usd=total_cost,
                    timestamp=datetime.utcnow().isoformat(),
                    output_file=None,
                    notes="Budget exceeded",
                )
            except Exception as e:
                logger.error(f"  Attempt {attempt+1} error: {e}")
                last_errors = [str(e)]

        # All attempts failed
        return GenerationResult(
            reward_name=req.reward_name,
            class_name=LLMRewardGenerator.extract_class_name(last_code) if last_code else "Unknown",
            mode=req.mode,
            success=False,
            generated_code=last_code,
            compiled=False,
            compile_errors=last_errors,
            attempts=attempts,
            api_cost_usd=total_cost,
            timestamp=datetime.utcnow().isoformat(),
            output_file=None,
            notes=f"Failed after {attempts} attempts",
        )


# ============================================================================
# CLI ENTRY POINT
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Phase 3-4: LLM-Based Reward Generation & Compilation Testing",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Export human-readable prompts (default, no API cost):
  %(prog)s                              # Export prompts for top 5 rewards
  %(prog)s --prompt-export --max-fixes 3  # Explicit prompt export

  # Validate user-provided reward code:
  %(prog)s --validate-code staging/MyReward.h  # Check compilation + safety

  # Use Claude API (automatic, requires ANTHROPIC_API_KEY):
  %(prog)s --api --max-fixes 3          # API generation with auto-testing
  %(prog)s --api --no-compile-test      # API generation, skip compilation

  # Other options:
  %(prog)s --dry-run                    # See what would be done
  %(prog)s --mode new --max-fixes 1     # Create a new reward (instead of fix)
  %(prog)s --checkpoint C:/path/to/cp   # Use specific checkpoint
"""
    )

    # Generation mode group (mutually exclusive)
    gen_group = parser.add_mutually_exclusive_group()
    gen_group.add_argument("--prompt-export", dest="generation_mode", action="store_const", const="prompt-export",
                          help="Export human-readable prompts (NO API, manual workflow, DEFAULT)")
    gen_group.add_argument("--api", dest="generation_mode", action="store_const", const="api",
                          help="Use Claude API directly (requires ANTHROPIC_API_KEY)")

    # Validation subcommand
    parser.add_argument("--validate-code", type=str, default=None,
                       help="Validate a user-provided reward .h file (static check + compile test)")

    # Common options
    parser.add_argument("--checkpoint", type=str, default=None,
                        help="Path to specific checkpoint directory (default: latest)")
    parser.add_argument("--checkpoints-root", type=str, default=None,
                        help=f"Path to checkpoints root (default: {DEFAULT_CHECKPOINTS_ROOT})")
    parser.add_argument("--mode", choices=["fix", "new", "both"], default="fix",
                        help="What to generate: fix existing, create new, or both (default: fix)")
    parser.add_argument("--max-fixes", type=int, default=5,
                        help="Maximum rewards to process per run (default: 5)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be done without generating files or API calls")
    parser.add_argument("--no-compile-test", action="store_true",
                        help="Skip compilation testing (all modes)")
    parser.add_argument("--model", type=str, default=None,
                        help=f"Claude model to use (--api mode only, default: {DEFAULT_MODEL})")

    args = parser.parse_args()

    # Set default generation mode to prompt-export
    if args.generation_mode is None:
        args.generation_mode = "prompt-export"

    # Setup logging
    logging.basicConfig(
        level=logging.INFO,
        format='[%(asctime)s] %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )

    print(f"\n{'='*70}")
    print(f"  Phase 3-4: LLM Reward Generation & Compilation Testing")
    print(f"  Project Root: {PROJECT_ROOT}")
    print(f"  Rewards Dir:  {REWARDS_DIR}")
    print(f"  Output Dir:   {STAGING_DIR}")
    print(f"{'='*70}\n")

    try:
        orchestrator = RewardGenerationOrchestrator(args)
        manifest = orchestrator.run()

        # Print summary
        successes = sum(1 for r in manifest.results if r.success)
        failures = len(manifest.results) - successes

        print(f"\n{'='*70}")
        print(f"  Generation Complete")
        print(f"  Successes: {successes}")
        print(f"  Failures:  {failures}")
        print(f"  Total Cost: ${manifest.total_api_cost_usd:.4f}")
        if manifest.results:
            print(f"  Staging Dir: {STAGING_DIR}")
            print(f"\n  Results:")
            for r in manifest.results:
                status = "OK" if r.success else "FAIL"
                print(f"    [{status}] {r.reward_name} -> {r.class_name} "
                      f"(attempts={r.attempts}, ${r.api_cost_usd:.4f})")
                if r.output_file:
                    print(f"           Output: {r.output_file}")
                if r.compile_errors:
                    print(f"           Errors: {r.compile_errors[0][:80]}")
        print(f"{'='*70}\n")

    except FileNotFoundError as e:
        logger.error(f"File not found: {e}")
        sys.exit(1)
    except ValueError as e:
        logger.error(f"Configuration error: {e}")
        sys.exit(1)
    except ImportError as e:
        logger.error(str(e))
        sys.exit(1)
    except Exception as e:
        logger.error(f"Unexpected error: {e}", exc_info=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
