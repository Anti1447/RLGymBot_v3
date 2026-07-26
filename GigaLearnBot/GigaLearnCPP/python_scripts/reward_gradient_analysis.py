"""
Reward Gradient Analysis for GigaLearnBot

Answers three questions from logged per-reward metrics
(RewardMetricsCollector: weight, mean_value, variance, activation_rate):

  1. DOMINANCE   - which rewards account for most of the gradient signal
  2. DEAD/SPARSE - which rewards contribute ~no signal (never fire, or fire
                   constantly with no variation)
  3. FARMING     - which rewards fire suspiciously often at a suspiciously
                   stable value (candidate for a degenerate exploit)

Two ways to use it:
  * As a library from the auto-analysis pipeline:
        analyze_gradients(current_metrics["rewards"])  # dict in, dict out
  * As a standalone CLI on a checkpoint snapshot or a W&B CSV export:
        python reward_gradient_analysis.py --json <checkpoint>/REWARD_METRICS.json
        python reward_gradient_analysis.py --csv  wandb_export.csv

No pandas dependency: this module is imported inside the embedded training
Python at checkpoint time, where reward_analysis_auto only relies on
numpy/matplotlib. Adding a mandatory pandas import here would risk re-breaking
the (deliberately non-fatal) checkpoint analysis, so everything below runs on
the stdlib. The CSV path uses the stdlib csv module.
"""
import argparse
import json
import re
import csv

DEAD_ACTIVATION_THRESHOLD = 0.02  # activation rate below this = effectively never fires
DEAD_DOM_PCT = 0.1                # ...and below this % of total gradient signal = dead
FARM_ACTIVATION_THRESHOLD = 0.60  # fires on more than 60% of steps
FARM_CV_THRESHOLD = 0.30          # ...at low coefficient of variation (std/|mean|)

# NOTE on "dead": we deliberately do NOT use an absolute variance floor. The values in
# REWARD_METRICS.json are averaged over ~50 arenas per sample (Learner.cpp), which
# deflates every reward's variance by ~50x. An absolute variance threshold (the original
# design's 1e-4) therefore misfires badly on this data -- it flagged GoalReward (the most
# dominant reward) and ~40 constantly-firing shaping rewards as "dead". Instead:
#   DEAD  = never fires (low activation) AND contributes negligible gradient share.
#           A dominant reward can never be dead by construction.
#   FARM  = fires very often at a suspiciously stable value. This also captures the
#           "constant bias" case (fires every step with almost no variation), which the
#           original design tried to catch with the variance floor.


def _row_from_metrics(name, m):
    return {
        "reward": name,
        "weight": float(m.get("weight", 0.0)),
        "mean_value": float(m.get("mean_value", 0.0)),
        "variance": float(m.get("variance", 0.0)),
        "activation_rate": float(m.get("activation_rate", 0.0)),
        "sample_count": float(m.get("sample_count", 0) or 0),
    }


def analyze_gradients(rewards):
    """Core analysis over a REWARD_METRICS.json['rewards'] dict.

    Args:
        rewards: { name: {weight, mean_value, variance, activation_rate, sample_count} }

    Returns a dict with:
        per_reward:             list of per-reward rows, sorted by dominance_pct desc.
                                Each row adds signal_variance, dominance_pct, cv,
                                dead, dead_reason, farming_candidate.
        dominant:               rows with dominance_pct > 10
        dead:                   rows flagged dead
        farming:                rows flagged farming_candidate
        total_signal_variance:  float, the Var(total) denominator
    """
    rows = [_row_from_metrics(n, m) for n, m in rewards.items()]

    # Gradient dominance: a reward's contribution to the VARIANCE of the total
    # combined reward, not its raw weight or mean. This is what actually matters
    # for gradient dilution -- a zero-sum reward can have mean ~0 but still swing
    # the advantage estimate on every step. Assuming independence across reward
    # terms, Var(total) ~= sum(weight_i^2 * Var(reward_i)), so each term's share
    # of that sum is its real share of the learning signal.
    #
    # weight^2 is correct here (not double-counting) because the variance in
    # REWARD_METRICS.json is of the RAW, UNWEIGHTED reward: EnvSet.cpp saves
    # output[player] (pre-weight; ZeroSum saves the inner pre-weight reward) into
    # lastRewards, and RewardMetricsCollector records exactly that. Verified in
    # source, not assumed -- see EnvSet.cpp:293-303 and RewardMetricsCollector.h.
    for r in rows:
        r["signal_variance"] = (r["weight"] ** 2) * r["variance"]

    total = sum(r["signal_variance"] for r in rows)

    for r in rows:
        r["dominance_pct"] = (r["signal_variance"] / total * 100.0) if total > 0 else 0.0

        std = r["variance"] ** 0.5
        r["cv"] = (std / abs(r["mean_value"])) if abs(r["mean_value"]) > 1e-6 else float("inf")

        # Dead = effectively never fires AND contributes negligible gradient signal.
        # The dominance guard is what protects rare-but-valuable event rewards
        # (Goal, Save, Demo, OwnGoalPenalty): they can have activation ~0 yet still
        # carry real signal because when they fire, weight^2 makes them dominate --
        # so a reward above the dominance floor is never dead.
        r["dead"] = (
            r["activation_rate"] < DEAD_ACTIVATION_THRESHOLD
            and r["dominance_pct"] < DEAD_DOM_PCT
        )
        r["dead_reason"] = "never fires" if r["dead"] else ""

        # Farming candidate: fires very often, at a very stable value. Also catches
        # the "constant bias" case (fires every step with almost no variation). This
        # is a *statistical* flag only -- per the "Reward Health != Behavior
        # Confirmation" split, confirm any hit here with RocketSimVis before touching
        # the reward. (No 'not dead' guard needed: the high activation requirement
        # already excludes anything flagged dead.)
        r["farming_candidate"] = (
            r["activation_rate"] > FARM_ACTIVATION_THRESHOLD
            and r["cv"] < FARM_CV_THRESHOLD
        )

    rows.sort(key=lambda r: r["dominance_pct"], reverse=True)

    return {
        "per_reward": rows,
        "dominant": [r for r in rows if r["dominance_pct"] > 10.0],
        "dead": [r for r in rows if r["dead"]],
        "farming": [r for r in rows if r["farming_candidate"]],
        "total_signal_variance": total,
    }


def wandb_metrics(result, top_n=8):
    """Flatten the analysis into flat scalars for wandb_run.log().

    Logs the three summary counts plus the top-N dominance percentages by name.
    top_n is capped so a config with ~80+ rewards doesn't explode into that many
    W&B series.
    """
    metrics = {
        "RewardGradient/dominant_count": len(result["dominant"]),
        "RewardGradient/dead_count": len(result["dead"]),
        "RewardGradient/farming_count": len(result["farming"]),
        "RewardGradient/total_signal_variance": result["total_signal_variance"],
    }
    for r in result["per_reward"][:top_n]:
        metrics[f"RewardGradient/dom_pct/{r['reward']}"] = r["dominance_pct"]
    return metrics


def _cv_json(cv):
    # JSON has no infinity; represent an undefined CV (mean ~0) as null.
    return None if cv == float("inf") else round(cv, 4)


def write_report(result, output_dir, total_timesteps, top_n=8):
    """Write a self-contained gradient snapshot into the reward_analysis folder.

    Mirrors the W&B log (same flat 'metrics' block that wandb_metrics produces)
    but also keeps the full ranked table and the flagged lists on disk. Overwritten
    each checkpoint, exactly like the graph PNGs in the same folder. Returns the
    written Path.
    """
    from pathlib import Path as _Path
    output_dir = _Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    def _slim(r):
        return {
            "reward": r["reward"],
            "weight": r["weight"],
            "dominance_pct": round(r["dominance_pct"], 4),
            "activation_rate": r["activation_rate"],
            "cv": _cv_json(r["cv"]),
            "dead": r["dead"],
            "farming_candidate": r["farming_candidate"],
        }

    payload = {
        "total_timesteps": total_timesteps,
        "metrics": wandb_metrics(result, top_n=top_n),
        "dominant": [_slim(r) for r in result["dominant"]],
        "dead": [{"reward": r["reward"], "activation_rate": r["activation_rate"]} for r in result["dead"]],
        "farming": [{"reward": r["reward"], "activation_rate": r["activation_rate"], "cv": _cv_json(r["cv"])} for r in result["farming"]],
        "per_reward": [_slim(r) for r in result["per_reward"]],
    }

    path = output_dir / "reward_gradient.json"
    path.write_text(json.dumps(payload, indent=2))
    return path


# ── Input loaders ──────────────────────────────────────────────────────────────

def load_from_json(path):
    with open(path) as f:
        data = json.load(f)
    return data["rewards"]


def load_from_csv(path):
    """Parse a W&B CSV export whose columns are 'rewards/<Name>/<field>'
    (W&B's default flattening of the nested JSON dict). Uses the last row
    (most recent logged step). If your export headers differ, the regex below
    is the one-line change."""
    pattern = re.compile(r"rewards/([^/]+)/(weight|mean_value|variance|activation_rate|sample_count)$")
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return {}
    last = rows[-1]
    rewards = {}
    for col, val in last.items():
        m = pattern.match(col)
        if not m:
            continue
        name, field = m.group(1), m.group(2)
        try:
            rewards.setdefault(name, {})[field] = float(val)
        except (TypeError, ValueError):
            pass
    return rewards


# ── Standalone reporting ─────────────────────────────────────────────────────────

def report(result):
    rows = result["per_reward"]
    print(f"{'Reward':<42}{'Weight':>8}{'Dom%':>8}{'ActRate':>9}{'CV':>8}  Flags")
    print("-" * 90)
    for r in rows:
        flags = []
        if r["dead"]:
            flags.append("DEAD")
        if r["farming_candidate"]:
            flags.append("FARM?")
        cv_str = f"{r['cv']:.2f}" if r["cv"] != float("inf") else "inf"
        print(f"{r['reward']:<42}{r['weight']:>8.2f}{r['dominance_pct']:>7.1f}%{r['activation_rate']:>9.2f}{cv_str:>8}  {' '.join(flags)}")

    print("\n--- Dominant rewards (>10% of total gradient-signal variance) ---")
    for r in result["dominant"]:
        print(f"  {r['reward']}: {r['dominance_pct']:.1f}%")

    print(f"\n--- Dead / near-dead rewards ({len(result['dead'])}) ---")
    for r in result["dead"]:
        print(f"  {r['reward']} ({r['dead_reason']})")

    print(f"\n--- Farming candidates ({len(result['farming'])}) -- confirm with RocketSimVis ---")
    for r in result["farming"]:
        print(f"  {r['reward']}: activation={r['activation_rate']:.2f}, cv={r['cv']:.2f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv")
    ap.add_argument("--json")
    args = ap.parse_args()
    if not args.json and not args.csv:
        ap.error("provide --json <REWARD_METRICS.json> or --csv <wandb_export.csv>")
    rewards = load_from_json(args.json) if args.json else load_from_csv(args.csv)
    report(analyze_gradients(rewards))


if __name__ == "__main__":
    main()
