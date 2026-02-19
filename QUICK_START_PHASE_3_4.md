# Quick Start: Phase 3-4 Reward Generation

## 3 Simple Commands

### 1. Export Prompts (Recommended for iteration)
```bash
python reward_llm_generator.py --max-fixes 3
```
Creates human-readable prompts in `generated_rewards/prompts/` that you can copy/paste into Claude Code.

**Output:**
- `Prompt_001_RewardName.txt` (copy to Claude Code)
- `Prompt_002_*.txt`, etc.
- `framework_requirements.txt` (reference)

**Cost:** $0.00

---

### 2. Validate Generated Code (After getting code from Claude Code)
```bash
python reward_llm_generator.py --validate-code staging/MyReward.h
```
Runs static checks and cl.exe compilation test on your reward code.

**Output:**
```
[OK] VALIDATION PASSED
  File: staging/MyReward.h
  Class: MyReward
  Ready for deployment to RLGymCPP/src/RLGymCPP/Rewards/
```

**Cost:** $0.00

---

### 3. Use Claude API (Advanced - once confident)
```bash
export ANTHROPIC_API_KEY=sk-ant-...
python reward_llm_generator.py --api --max-fixes 1
```
Automatically generates, tests, and saves code to `staging/`.

**Cost:** ~$0.01-0.05 per reward

---

## Typical Workflow

```bash
# Step 1: Export prompts (zero cost)
python reward_llm_generator.py --max-fixes 1

# Step 2: User manually
#   - Open generated_rewards/prompts/Prompt_001_*.txt
#   - Copy entire prompt
#   - Paste into Claude Code
#   - Get Claude's generated code
#   - Copy code to staging/MyReward.h

# Step 3: Validate (zero cost)
python reward_llm_generator.py --validate-code staging/MyReward.h

# Step 4: Deploy if validation passes
#   cp staging/MyReward.h GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/
#   # Edit RewardConfig.cpp to add weight + entry
#   # Rebuild and resume training
```

---

## Command Cheat Sheet

| Command | Purpose | Cost |
|---------|---------|------|
| `python reward_llm_generator.py` | Export prompts (default) | $0 |
| `python reward_llm_generator.py --max-fixes 3` | Export 3 prompts | $0 |
| `python reward_llm_generator.py --validate-code file.h` | Validate code locally | $0 |
| `python reward_llm_generator.py --api` | Auto-generate with API | ~$0.01-0.05 |
| `python reward_llm_generator.py --dry-run` | Preview without creating files | $0 |

---

## Default Behavior

**No API key needed.** Just run:
```bash
python reward_llm_generator.py
```

Defaults to `--prompt-export` mode (zero cost, manual workflow).

---

## Troubleshooting

**Q: "CompilationTester not found"**
- This is OK — just means MSVC cl.exe isn't on PATH
- Use `--no-compile-test` to skip compilation check

**Q: "ANTHROPIC_API_KEY error" when using `--api`**
- Set the key first: `export ANTHROPIC_API_KEY=sk-ant-...`
- `--api` mode requires a key; other modes don't

**Q: Where are the generated files?**
- Prompts: `build/generated_rewards/prompts/`
- Your code after validation: `build/generated_rewards/staging/`
- Framework reference: `build/generated_rewards/framework_requirements.txt`

---

## Pro Tips

1. **Start with `--prompt-export`** — it's safe, free, and teaches you the reward framework
2. **Read the generated prompts** — they show exactly what Claude will be asked
3. **Validate locally before deploying** — catch errors before they get into training
4. **Keep `--api` for batch work** — once you're confident in the code quality

