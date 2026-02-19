"""
Automated Reward Analysis Pipeline
Called at every checkpoint save to generate comparative analysis.
Standalone - no Claude API calls required.

Entry point: run_analysis(checkpoint_path, checkpoints_root, total_timesteps, wandb_run)
"""

import json
import os
import time
from pathlib import Path

# ── Lazy imports for heavy modules ────────────────────────────────────────────
_plt = None
_np = None
_matplotlib = None

HISTORY_FILENAME = "reward_analysis_history.json"

def _ensure_imports():
    global _plt, _np, _matplotlib
    if _plt is None:
        import matplotlib
        matplotlib.use('Agg')  # Non-interactive backend for embedded/headless use
        import matplotlib.pyplot as plt
        import matplotlib.gridspec as gridspec
        import numpy as np
        _plt = plt
        _np = np
        _matplotlib = matplotlib

        # Dark theme
        plt.rcParams.update({
            'figure.facecolor': '#0D1117',
            'axes.facecolor': '#161B22',
            'axes.edgecolor': '#30363D',
            'axes.labelcolor': '#C9D1D9',
            'text.color': '#C9D1D9',
            'xtick.color': '#C9D1D9',
            'ytick.color': '#C9D1D9',
            'grid.color': '#30363D',
            'grid.alpha': 0.3,
            'font.family': 'sans-serif',
            'font.size': 10,
        })


# ── Color constants ───────────────────────────────────────────────────────────
CLR_ACCENT  = "#FF6B35"
CLR_ACCENT2 = "#4ECDC4"
CLR_WARN    = "#E63946"
CLR_GOOD    = "#2EC4B6"
CLR_DARK    = "#1D3557"
CLR_MED     = "#457B9D"
CLR_LIGHT   = "#A8DADC"
CLR_BG      = "#0D1117"
CLR_CARD    = "#161B22"
CLR_TEXT    = "#C9D1D9"
CLR_GRID    = "#30363D"


# ═══════════════════════════════════════════════════════════════════════════════
# PUBLIC API
# ═══════════════════════════════════════════════════════════════════════════════

def run_analysis(checkpoint_path, checkpoints_root, total_timesteps, wandb_run=None):
    """Main entry point. Called from metric_receiver.on_checkpoint_saved().

    Args:
        checkpoint_path: str or Path to the specific checkpoint folder (e.g., checkpoints/99600260964/)
        checkpoints_root: str or Path to the parent checkpoints folder
        total_timesteps: int, current total timesteps
        wandb_run: optional wandb run object for logging metrics
    """
    t0 = time.time()

    checkpoint_path = Path(checkpoint_path)
    checkpoints_root = Path(checkpoints_root)

    # 1. Validate current checkpoint has required files
    metrics_file = checkpoint_path / "REWARD_METRICS.json"
    diagnosis_file = checkpoint_path / "REWARD_DIAGNOSIS.json"
    if not metrics_file.exists() or not diagnosis_file.exists():
        print(f"[RewardAnalysis] Missing files in {checkpoint_path}, skipping.")
        return

    # 2. Find previous checkpoint
    prev_checkpoint = _find_previous_checkpoint(checkpoint_path, checkpoints_root)

    # 3. Load data
    current_metrics = json.loads(metrics_file.read_text())
    current_diagnosis = json.loads(diagnosis_file.read_text())

    prev_metrics = None
    prev_diagnosis = None
    if prev_checkpoint:
        prev_mf = prev_checkpoint / "REWARD_METRICS.json"
        prev_df = prev_checkpoint / "REWARD_DIAGNOSIS.json"
        if prev_mf.exists():
            prev_metrics = json.loads(prev_mf.read_text())
        if prev_df.exists():
            prev_diagnosis = json.loads(prev_df.read_text())

    # 4. Update persistent history
    history = _load_or_create_history(checkpoints_root)
    history = _update_history(history, current_metrics, current_diagnosis, total_timesteps)
    _save_history(history, checkpoints_root)

    # 5. Compute summary metrics
    summary = _compute_summary(current_metrics, current_diagnosis, prev_metrics)

    # 6. Log to wandb
    if wandb_run is not None:
        _log_to_wandb(wandb_run, summary, checkpoints_root)

    # 7. Generate graphs (need 2+ checkpoints for comparison)
    if prev_metrics is not None:
        _ensure_imports()
        output_dir = checkpoints_root / "reward_analysis"
        output_dir.mkdir(exist_ok=True)
        _generate_graphs(
            current_metrics, current_diagnosis,
            prev_metrics, prev_diagnosis,
            history, output_dir, total_timesteps
        )
        # Log graph images to wandb
        if wandb_run is not None:
            _log_graphs_to_wandb(wandb_run, output_dir)

    elapsed = time.time() - t0
    print(f"[RewardAnalysis] Analysis complete for checkpoint {total_timesteps} ({elapsed:.2f}s)")


# ═══════════════════════════════════════════════════════════════════════════════
# CHECKPOINT DISCOVERY
# ═══════════════════════════════════════════════════════════════════════════════

def _find_previous_checkpoint(current_path, checkpoints_root):
    """Find the most recent checkpoint before current_path that has REWARD_METRICS.json."""
    current_ts = int(current_path.name)

    candidates = []
    for entry in checkpoints_root.iterdir():
        if entry.is_dir() and entry.name.isdigit():
            ts = int(entry.name)
            if ts < current_ts and (entry / "REWARD_METRICS.json").exists():
                candidates.append((ts, entry))

    if not candidates:
        return None

    candidates.sort(key=lambda x: x[0], reverse=True)
    return candidates[0][1]


# ═══════════════════════════════════════════════════════════════════════════════
# HISTORY MANAGEMENT
# ═══════════════════════════════════════════════════════════════════════════════

def _load_or_create_history(checkpoints_root):
    history_path = checkpoints_root / HISTORY_FILENAME
    if history_path.exists():
        try:
            return json.loads(history_path.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {"version": 1, "entries": []}


def _update_history(history, metrics, diagnosis, total_timesteps):
    # Avoid duplicate entries
    existing_ts = {e.get("total_timesteps") for e in history.get("entries", [])}
    if total_timesteps in existing_ts:
        return history

    rewards = metrics.get("rewards", {})
    active_count = sum(1 for r in rewards.values() if r.get("activation_rate", 0) > 0.001)
    dead_count = sum(1 for r in rewards.values() if r.get("activation_rate", 0) == 0)
    low_act = sum(1 for r in rewards.values() if 0 < r.get("activation_rate", 0) < 0.05)
    always_on = sum(1 for r in rewards.values() if r.get("activation_rate", 0) >= 0.99)
    total_wc = sum(abs(r.get("mean_value", 0)) * r.get("weight", 0) for r in rewards.values())

    entry = {
        "total_timesteps": total_timesteps,
        "timestamp_utc": metrics.get("timestamp_utc", int(time.time())),
        "reward_count": len(rewards),
        "active_count": active_count,
        "dead_count": dead_count,
        "low_activation_count": low_act,
        "always_on_count": always_on,
        "healthy_count": diagnosis.get("healthy_reward_count", 0),
        "problematic_count": diagnosis.get("problematic_reward_count", 0),
        "total_weighted_contribution": round(total_wc, 6),
        "skill_ratings": metrics.get("skill_ratings", {}),
        "skill_rating_deltas": metrics.get("skill_rating_deltas", {}),
        "diagnosis_counts": _count_diagnosis_types(diagnosis),
    }

    history["entries"].append(entry)
    return history


def _save_history(history, checkpoints_root):
    history_path = checkpoints_root / HISTORY_FILENAME
    tmp_path = checkpoints_root / (HISTORY_FILENAME + ".tmp")
    try:
        with open(tmp_path, 'w') as f:
            json.dump(history, f, indent=2)
        if history_path.exists():
            history_path.unlink()
        tmp_path.rename(history_path)
    except OSError as e:
        print(f"[RewardAnalysis] WARNING: Failed to save history: {e}")


def _count_diagnosis_types(diagnosis):
    counts = {}
    for d in diagnosis.get("diagnoses", []):
        t = d.get("problem_type", "UNKNOWN")
        counts[t] = counts.get(t, 0) + 1
    return counts


# ═══════════════════════════════════════════════════════════════════════════════
# SUMMARY COMPUTATION
# ═══════════════════════════════════════════════════════════════════════════════

def _compute_summary(current_metrics, current_diagnosis, prev_metrics):
    rewards = current_metrics.get("rewards", {})

    active_count = sum(1 for r in rewards.values() if r.get("activation_rate", 0) > 0.001)
    dead_count = sum(1 for r in rewards.values() if r.get("activation_rate", 0) == 0)
    low_act = sum(1 for r in rewards.values() if 0 < r.get("activation_rate", 0) < 0.05)
    always_on = sum(1 for r in rewards.values() if r.get("activation_rate", 0) >= 0.99)

    summary = {
        "reward_count_total": len(rewards),
        "reward_count_active": active_count,
        "reward_count_dead": dead_count,
        "reward_count_low_activation": low_act,
        "reward_count_always_on": always_on,
        "healthy_reward_count": current_diagnosis.get("healthy_reward_count", 0),
        "problematic_reward_count": current_diagnosis.get("problematic_reward_count", 0),
    }

    # Diagnosis type counts
    for dtype, count in _count_diagnosis_types(current_diagnosis).items():
        summary[f"diagnosis_{dtype.lower()}"] = count

    # Skill ratings
    for mode, rating in current_metrics.get("skill_ratings", {}).items():
        summary[f"skill_rating_{mode}"] = rating
    for mode, delta in current_metrics.get("skill_rating_deltas", {}).items():
        summary[f"skill_rating_delta_{mode}"] = delta

    # Reward config change detection
    if prev_metrics:
        prev_names = set(prev_metrics.get("rewards", {}).keys())
        curr_names = set(rewards.keys())
        summary["rewards_added_count"] = len(curr_names - prev_names)
        summary["rewards_removed_count"] = len(prev_names - curr_names)

    # Gradient signal metrics
    total_wc = sum(abs(r.get("mean_value", 0)) * r.get("weight", 0) for r in rewards.values())
    summary["total_weighted_contribution"] = total_wc
    summary["gradient_dilution_active_ratio"] = active_count / max(1, len(rewards))

    return summary


# ═══════════════════════════════════════════════════════════════════════════════
# WANDB LOGGING
# ═══════════════════════════════════════════════════════════════════════════════

def _log_to_wandb(wandb_run, summary, checkpoints_root):
    log_dict = {}
    for key, value in summary.items():
        if isinstance(value, (int, float)):
            log_dict[f"RewardAnalysis/{key}"] = value
    try:
        wandb_run.log(log_dict)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: wandb scalar logging failed: {e}")


def _log_graphs_to_wandb(wandb_run, output_dir):
    try:
        import wandb
        log_dict = {}
        for png_file in sorted(output_dir.glob("*.png")):
            try:
                log_dict[f"RewardAnalysis/Graphs/{png_file.stem}"] = wandb.Image(str(png_file))
            except Exception:
                pass
        if log_dict:
            wandb_run.log(log_dict)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: wandb image logging failed: {e}")


# ═══════════════════════════════════════════════════════════════════════════════
# HELPER FUNCTIONS
# ═══════════════════════════════════════════════════════════════════════════════

def _short_name(name, max_len=28):
    name = name.replace("Reward", "").replace("Penalty", "Pen")
    name = name.replace("MathRewardWrapper(MULTIPLY:[", "").replace("])", "")
    name = name.replace("PlayerDataEvent", "PDE")
    name = name.replace(",ShotAccuracyInBounds", "*ShotAcc")
    if len(name) > max_len:
        name = name[:max_len - 2] + ".."
    return name


# ═══════════════════════════════════════════════════════════════════════════════
# GRAPH GENERATION
# ═══════════════════════════════════════════════════════════════════════════════

def _generate_graphs(curr_m, curr_d, prev_m, prev_d, history, output_dir, total_timesteps):
    """Generate all analysis graphs."""
    plt = _plt
    np = _np
    from matplotlib.patches import Patch

    # ── Build data arrays ─────────────────────────────────────────────────
    curr_rewards = curr_m.get("rewards", {})
    prev_rewards = prev_m.get("rewards", {}) if prev_m else {}
    all_curr_names = sorted(curr_rewards.keys())
    common_names = sorted(set(curr_rewards.keys()) & set(prev_rewards.keys()))
    added_names = set(curr_rewards.keys()) - set(prev_rewards.keys())
    removed_names = set(prev_rewards.keys()) - set(curr_rewards.keys())

    names = all_curr_names
    activations = [curr_rewards[n]["activation_rate"] for n in names]
    means = [curr_rewards[n]["mean_value"] for n in names]
    variances = [curr_rewards[n]["variance"] for n in names]
    weights = [curr_rewards[n]["weight"] for n in names]
    weighted_contribs = [abs(curr_rewards[n]["mean_value"]) * curr_rewards[n]["weight"] for n in names]

    prev_activations = {n: prev_rewards[n]["activation_rate"] for n in common_names} if prev_rewards else {}
    prev_means = {n: prev_rewards[n]["mean_value"] for n in common_names} if prev_rewards else {}

    # ── Figure 1: Dashboard Overview ──────────────────────────────────────
    try:
        _fig1_dashboard(plt, np, Patch, curr_m, curr_d, prev_m, names, activations,
                        weights, weighted_contribs, added_names, removed_names, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 1 failed: {e}")

    # ── Figure 2: Top 20 Weighted Contribution ────────────────────────────
    try:
        _fig2_top20(plt, np, Patch, names, activations, means, weights, weighted_contribs, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 2 failed: {e}")

    # ── Figure 3: Weight vs Activation Scatter ────────────────────────────
    try:
        _fig3_scatter(plt, np, Patch, names, activations, weights, curr_d, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 3 failed: {e}")

    # ── Figure 4: Activation Changes ──────────────────────────────────────
    try:
        if common_names:
            _fig4_activation_changes(plt, np, common_names, curr_rewards, prev_rewards, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 4 failed: {e}")

    # ── Figure 5: Activation Heatmap ──────────────────────────────────────
    try:
        if common_names:
            _fig5_heatmap(plt, np, curr_m, prev_m, common_names, curr_rewards, prev_rewards, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 5 failed: {e}")

    # ── Figure 6: Scaling & Categories ────────────────────────────────────
    try:
        _fig6_scaling(plt, np, Patch, names, activations, means, weights, weighted_contribs,
                      curr_rewards, curr_d, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 6 failed: {e}")

    # ── Figure 7: Variance Analysis ───────────────────────────────────────
    try:
        _fig7_variance(plt, np, names, activations, means, variances, weights, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 7 failed: {e}")

    # ── Figure 8: Mean Value Changes ──────────────────────────────────────
    try:
        if common_names:
            _fig8_mean_changes(plt, np, common_names, curr_rewards, prev_rewards, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 8 failed: {e}")

    # ── Figure 9: Skill Rating Trends (from history) ──────────────────────
    try:
        entries = history.get("entries", [])
        if len(entries) >= 2:
            _fig9_skill_trends(plt, np, entries, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 9 failed: {e}")

    # ── Figure 10: Reward Health Trends (from history) ────────────────────
    try:
        entries = history.get("entries", [])
        if len(entries) >= 2:
            _fig10_health_trends(plt, np, entries, output_dir)
    except Exception as e:
        print(f"[RewardAnalysis] WARNING: Fig 10 failed: {e}")

    plt.close('all')


# ── Individual Figure Functions ───────────────────────────────────────────────

def _fig1_dashboard(plt, np, Patch, curr_m, curr_d, prev_m, names, activations,
                    weights, weighted_contribs, added_names, removed_names, output_dir):
    fig, axes = plt.subplots(2, 2, figsize=(18, 12))
    fig.suptitle(f"Reward Diagnostics Dashboard — {curr_m.get('reward_count', '?')} Rewards @ {curr_m.get('total_timesteps', 0)/1e9:.2f}B Steps",
                 fontsize=16, fontweight='bold', color=CLR_ACCENT, y=0.98)

    # 1a: Diagnosis Breakdown Donut
    ax = axes[0, 0]
    diag_types = {}
    for d in curr_d.get("diagnoses", []):
        t = d["problem_type"]
        diag_types[t] = diag_types.get(t, 0) + 1
    labels = list(diag_types.keys()) + ["HEALTHY"]
    sizes = list(diag_types.values()) + [curr_d.get("healthy_reward_count", 0)]
    color_map = {"LOW_ACTIVATION": CLR_WARN, "INCORRECT_SCALING": CLR_ACCENT,
                 "GRADIENT_DILUTION": "#FCA311", "HEALTHY": CLR_GOOD,
                 "HIGH_VARIANCE": "#9B59B6", "CONFLICTING": "#E74C3C",
                 "POOR_WIN_CORRELATION": CLR_MED}
    donut_colors = [color_map.get(l, CLR_MED) for l in labels]

    if sum(sizes) > 0:
        wedges, texts, autotexts = ax.pie(sizes, labels=None, autopct='%1.0f%%', startangle=90,
                                          colors=donut_colors, pctdistance=0.82,
                                          wedgeprops=dict(width=0.4, edgecolor=CLR_BG, linewidth=2))
        for t in autotexts:
            t.set_fontsize(10); t.set_color('white'); t.set_fontweight('bold')
    ax.legend([f"{l} ({s})" for l, s in zip(labels, sizes)],
              loc='center', fontsize=9, frameon=False, labelcolor=CLR_TEXT)
    ax.set_title("Diagnosis Breakdown", fontsize=12, fontweight='bold', pad=10)

    # 1b: Skill Rating Deltas
    ax = axes[0, 1]
    modes = list(curr_m.get("skill_rating_deltas", {}).keys())
    if modes and prev_m:
        deltas_prev = [prev_m.get("skill_rating_deltas", {}).get(m, 0) for m in modes]
        deltas_curr = [curr_m["skill_rating_deltas"][m] for m in modes]
        ratings_curr = [curr_m.get("skill_ratings", {}).get(m, 0) for m in modes]
        x = np.arange(len(modes))
        w = 0.35
        bars1 = ax.bar(x - w/2, deltas_prev, w, label=f'Previous CP', color=CLR_ACCENT2, edgecolor=CLR_BG)
        bars2 = ax.bar(x + w/2, deltas_curr, w, label=f'Current CP', color=CLR_ACCENT, edgecolor=CLR_BG)
        for bar in list(bars1) + list(bars2):
            h = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., h + (0.5 if h >= 0 else -1.5),
                    f'{h:+.1f}', ha='center', va='bottom' if h >= 0 else 'top', fontsize=9, fontweight='bold')
        ax.axhline(y=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
        ax.set_xticks(x); ax.set_xticklabels(modes, fontsize=11)
        ax.legend(fontsize=9, frameon=False)
    ax.set_title("Skill Rating Deltas", fontsize=12, fontweight='bold', pad=10)
    ax.set_ylabel("ELO Delta"); ax.grid(axis='y', alpha=0.2)

    # 1c: Activation Rate Distribution
    ax = axes[1, 0]
    bins = [0, 0.01, 0.05, 0.1, 0.25, 0.5, 0.75, 0.9, 1.01]
    bin_labels = ["0%\n(Dead)", "<5%\n(Low)", "5-10%", "10-25%", "25-50%", "50-75%", "75-90%", "90-100%"]
    hist_vals, _ = np.histogram(activations, bins=bins)
    bar_colors = [CLR_WARN, CLR_WARN, CLR_ACCENT, CLR_ACCENT, CLR_MED, CLR_MED, CLR_GOOD, CLR_GOOD]
    bars = ax.bar(range(len(hist_vals)), hist_vals, color=bar_colors, edgecolor=CLR_BG)
    for bar, val in zip(bars, hist_vals):
        if val > 0:
            ax.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 0.3,
                    str(val), ha='center', va='bottom', fontsize=10, fontweight='bold')
    ax.set_xticks(range(len(bin_labels))); ax.set_xticklabels(bin_labels, fontsize=8)
    ax.set_ylabel("Number of Rewards")
    ax.set_title(f"Activation Rate Distribution ({len(names)} Rewards)", fontsize=12, fontweight='bold', pad=10)
    ax.grid(axis='y', alpha=0.2)

    # 1d: Summary Stats
    ax = axes[1, 1]; ax.axis('off')
    active_count = sum(1 for a in activations if a > 0.001)
    dead_count = sum(1 for a in activations if a == 0)
    low_count = sum(1 for a in activations if 0 < a < 0.05)
    on_count = sum(1 for a in activations if a >= 0.99)
    dead_names = [_short_name(n) for n, a in zip(names, activations) if a == 0]
    ratings = curr_m.get("skill_ratings", {})
    deltas = curr_m.get("skill_rating_deltas", {})

    lines = [
        f"  Total Rewards:        {len(names)}",
        f"  Active Rewards:       {active_count} (threshold: >35)",
        f"  Dead Rewards (0%):    {dead_count}",
        f"  Low Activation (<5%): {low_count}",
        f"  Always-On (>=99%):    {on_count}",
        f"  Healthy:              {curr_d.get('healthy_reward_count', '?')}",
        f"  Problematic:          {curr_d.get('problematic_reward_count', '?')}",
        f"",
    ]
    for m in sorted(ratings.keys()):
        d = deltas.get(m, 0)
        lines.append(f"  {m}: {ratings[m]:.1f}  (delta {d:+.1f})")
    if dead_names:
        lines.append(f"")
        lines.append(f"  Dead: {', '.join(dead_names[:5])}")
    if added_names:
        lines.append(f"  Added: {', '.join(_short_name(n) for n in sorted(added_names)[:5])}")
    if removed_names:
        lines.append(f"  Removed: {', '.join(_short_name(n) for n in sorted(removed_names)[:5])}")

    summary_text = "\n".join(lines)
    ax.text(0.05, 0.95, summary_text, transform=ax.transAxes, fontsize=9,
            verticalalignment='top', fontfamily='monospace',
            bbox=dict(boxstyle='round,pad=0.5', facecolor=CLR_CARD, edgecolor=CLR_GRID, alpha=0.9))

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(output_dir / "01_dashboard_overview.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig2_top20(plt, np, Patch, names, activations, means, weights, weighted_contribs, output_dir):
    fig, ax = plt.subplots(figsize=(16, 10))
    sorted_idx = sorted(range(len(names)), key=lambda i: weighted_contribs[i], reverse=True)[:20]
    top_names = [_short_name(names[i]) for i in sorted_idx]
    top_contribs = [weighted_contribs[i] for i in sorted_idx]

    y = np.arange(len(top_names))
    colors = []
    for i in sorted_idx:
        if activations[i] >= 0.9: colors.append(CLR_GOOD)
        elif activations[i] >= 0.3: colors.append(CLR_MED)
        elif activations[i] >= 0.05: colors.append(CLR_ACCENT)
        else: colors.append(CLR_WARN)

    bars = ax.barh(y, top_contribs, color=colors, edgecolor=CLR_BG, height=0.7)
    for i, idx in enumerate(sorted_idx):
        w = bars[i].get_width()
        ax.text(w + 0.02, bars[i].get_y() + bars[i].get_height()/2.,
                f'w={weights[idx]:.0f}  u={means[idx]:.4f}  act={activations[idx]:.0%}',
                ha='left', va='center', fontsize=8, color=CLR_LIGHT)

    ax.set_yticks(y); ax.set_yticklabels(top_names, fontsize=9); ax.invert_yaxis()
    ax.set_xlabel("Weighted Contribution (weight x |mean|)", fontsize=11)
    ax.set_title("Top 20 Rewards by Gradient Influence", fontsize=14, fontweight='bold', pad=15)
    ax.grid(axis='x', alpha=0.2)
    legend_elements = [Patch(facecolor=CLR_GOOD, label='>=90%'), Patch(facecolor=CLR_MED, label='30-90%'),
                       Patch(facecolor=CLR_ACCENT, label='5-30%'), Patch(facecolor=CLR_WARN, label='<5%')]
    ax.legend(handles=legend_elements, loc='lower right', fontsize=9, frameon=False, title='Activation')
    fig.tight_layout()
    fig.savefig(output_dir / "02_top20_weighted_contribution.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig3_scatter(plt, np, Patch, names, activations, weights, curr_d, output_dir):
    fig, ax = plt.subplots(figsize=(14, 10))
    incorrect_names = {d["reward_name"] for d in curr_d.get("diagnoses", [])
                       if d["problem_type"] == "INCORRECT_SCALING"}

    colors_s, sizes_s = [], []
    for i, name in enumerate(names):
        if name in incorrect_names: colors_s.append(CLR_WARN); sizes_s.append(120)
        elif activations[i] == 0: colors_s.append("#555555"); sizes_s.append(80)
        else: colors_s.append(CLR_ACCENT2); sizes_s.append(50)

    ax.scatter(activations, weights, c=colors_s, s=sizes_s, alpha=0.7, edgecolors=CLR_BG, linewidth=0.5)
    for i, name in enumerate(names):
        if name in incorrect_names or activations[i] == 0:
            ax.annotate(_short_name(name, 22), (activations[i], weights[i]),
                        textcoords="offset points", xytext=(8, 5), fontsize=7.5,
                        color=CLR_WARN if name in incorrect_names else "#888888",
                        arrowprops=dict(arrowstyle='-', color=CLR_GRID, lw=0.5))

    ax.axvspan(0, 0.05, alpha=0.08, color=CLR_WARN)
    ax.axhline(y=35, color=CLR_ACCENT, linewidth=1, linestyle='--', alpha=0.5)
    legend_elements = [Patch(facecolor=CLR_WARN, label='INCORRECT_SCALING'),
                       Patch(facecolor="#555555", label='Dead (0%)'),
                       Patch(facecolor=CLR_ACCENT2, label='Normal')]
    ax.legend(handles=legend_elements, loc='upper right', fontsize=9, frameon=False)
    ax.set_xlabel("Activation Rate"); ax.set_ylabel("Weight")
    ax.set_title("Weight vs Activation — Scaling Mismatches", fontsize=14, fontweight='bold', pad=15)
    ax.grid(alpha=0.2); ax.set_xlim(-0.03, 1.05)
    fig.tight_layout()
    fig.savefig(output_dir / "03_weight_vs_activation.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig4_activation_changes(plt, np, common_names, curr_rewards, prev_rewards, output_dir):
    fig, ax = plt.subplots(figsize=(16, 10))
    changes = [(n, curr_rewards[n]["activation_rate"] - prev_rewards[n]["activation_rate"]) for n in common_names]
    changes.sort(key=lambda x: abs(x[1]), reverse=True)
    top = changes[:25]

    y = np.arange(len(top))
    vals = [c[1] * 100 for c in top]
    colors = [CLR_GOOD if v > 0 else CLR_WARN for v in vals]
    bars = ax.barh(y, vals, color=colors, edgecolor=CLR_BG, height=0.7)
    for i, (name, change) in enumerate(top):
        w = bars[i].get_width()
        prev_a = prev_rewards[name]["activation_rate"]
        curr_a = curr_rewards[name]["activation_rate"]
        ax.text(w + (0.3 if w >= 0 else -0.3), bars[i].get_y() + bars[i].get_height()/2.,
                f'{prev_a:.1%} -> {curr_a:.1%}', ha='left' if w >= 0 else 'right', va='center',
                fontsize=8, color=CLR_LIGHT)

    ax.set_yticks(y); ax.set_yticklabels([_short_name(c[0]) for c in top], fontsize=9)
    ax.invert_yaxis(); ax.axvline(x=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
    ax.set_xlabel("Change in Activation Rate (percentage points)")
    ax.set_title("Top 25 Activation Rate Changes", fontsize=14, fontweight='bold', pad=15)
    ax.grid(axis='x', alpha=0.2)
    fig.tight_layout()
    fig.savefig(output_dir / "04_activation_changes.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig5_heatmap(plt, np, curr_m, prev_m, common_names, curr_rewards, prev_rewards, output_dir):
    fig, ax = plt.subplots(figsize=(18, 16))
    sorted_names = sorted(common_names, key=lambda n: curr_rewards[n]["activation_rate"])
    hm_names = [_short_name(n, 30) for n in sorted_names]
    data = np.array([[prev_rewards[n]["activation_rate"], curr_rewards[n]["activation_rate"]] for n in sorted_names])

    im = ax.imshow(data, aspect='auto', cmap='RdYlGn', vmin=0, vmax=1)
    ax.set_xticks([0, 1])
    ax.set_xticklabels([f'Previous\n({prev_m.get("total_timesteps",0)/1e9:.2f}B)',
                        f'Current\n({curr_m.get("total_timesteps",0)/1e9:.2f}B)'], fontsize=10)
    ax.set_yticks(range(len(hm_names))); ax.set_yticklabels(hm_names, fontsize=6.5)
    ax.set_title("Activation Rate Heatmap (Common Rewards)", fontsize=14, fontweight='bold', pad=15)
    for i in range(len(sorted_names)):
        for j in range(2):
            val = data[i, j]
            if val < 0.3:
                ax.text(j, i, f'{val:.1%}', ha='center', va='center', fontsize=5.5,
                        color='white' if val < 0.15 else 'black', fontweight='bold')
    plt.colorbar(im, ax=ax, shrink=0.6, label='Activation Rate')
    fig.tight_layout()
    fig.savefig(output_dir / "05_activation_heatmap.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig6_scaling(plt, np, Patch, names, activations, means, weights, weighted_contribs,
                  curr_rewards, curr_d, output_dir):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))

    # Left: INCORRECT_SCALING detail
    scaling_diag = [d for d in curr_d.get("diagnoses", []) if d["problem_type"] == "INCORRECT_SCALING"]
    if scaling_diag:
        s_names = [_short_name(d["reward_name"], 24) for d in scaling_diag]
        s_weights = [curr_rewards[d["reward_name"]]["weight"] for d in scaling_diag]
        s_contribs = [abs(curr_rewards[d["reward_name"]]["mean_value"]) * curr_rewards[d["reward_name"]]["weight"]
                      for d in scaling_diag]
        y = np.arange(len(s_names))
        ax1.barh(y - 0.2, s_weights, 0.4, label='Weight', color=CLR_ACCENT, edgecolor=CLR_BG)
        ax1_twin = ax1.twiny()
        ax1_twin.barh(y + 0.2, s_contribs, 0.4, label='Contrib', color=CLR_ACCENT2, edgecolor=CLR_BG)
        ax1.set_yticks(y); ax1.set_yticklabels(s_names, fontsize=9)
        ax1.set_xlabel("Weight", color=CLR_ACCENT)
        ax1_twin.set_xlabel("Weighted Contribution", color=CLR_ACCENT2)
        ax1_twin.tick_params(axis='x', colors=CLR_ACCENT2)
        ax1.invert_yaxis()
    ax1.set_title("INCORRECT_SCALING\nHigh Weight, Low Impact", fontsize=12, fontweight='bold', pad=15)
    ax1.grid(axis='x', alpha=0.2)

    # Right: Category breakdown
    categories = {
        "Boost Mgmt": ["SaveBoost", "BoostStarvation", "PickupBoost", "BoostAwareRotation",
                        "BoostPath", "BoostWastePenalty", "BoostSteal", "SupportingPlayerBoostMaintenance",
                        "DefensiveRotationBoost", "BigBoostPadCenter", "SupersonicBoostPenalty", "SmallPadCollection"],
        "Ball Control": ["Dribble", "DribbleMastery", "DribbleSetup", "AirDribble", "AirDribbleSetup",
                         "AirDribbleAccuracy", "AirDribbleClearing", "CatchBallOnCar", "PossessionControl"],
        "Shooting": ["GoalReward", "VelocityBallToGoal", "StrongTouch", "TouchAccel", "WallShot",
                     "DoomseeDisshShot", "MathRewardWrapper"],
        "Defense": ["DefensivePosition", "DefensivePositioningChallenge", "BackboardDefense",
                    "ShadowDefenseVsPossession", "GoalpostPositioning", "GoalLineClearance",
                    "BigClear", "CornerClear", "EmergencySaveCommit"],
        "Flicks": ["BackFlipFlick", "AdvancedFlick", "FlickExploration", "MustyFlick"],
        "Positioning": ["ProgressiveRotation", "PassingSetup", "ReceivingPass", "ActiveSupportingPosition",
                        "DirectionChange", "RetreatAfterTouch", "NumericalAdvantageAttacking"],
        "Mechanics": ["EnhancedSpeedFlip", "AdvancedRecovery", "PowerSlideControl", "EarlyAerial",
                      "AerialCommitment", "ProperLanding", "HalfFlip", "WavedashBasic"],
        "Penalties": ["DoubleCommitPenalty", "SimpleOvercommitmentPenalty", "AdvancedOvercommitmentPenalty",
                      "PassToOpponentPenalty", "OwnNetCenteringPenalty"],
    }
    cat_contribs = {}
    for cat, keywords in categories.items():
        total = 0
        for i, name in enumerate(names):
            for kw in keywords:
                if kw.lower() in name.lower():
                    total += weighted_contribs[i]; break
        cat_contribs[cat] = total

    sorted_cats = sorted(cat_contribs.items(), key=lambda x: x[1], reverse=True)
    colors_cat = [CLR_ACCENT2, CLR_MED, CLR_ACCENT, CLR_DARK, CLR_GOOD, CLR_LIGHT, "#FCA311", CLR_WARN]
    bars = ax2.barh(range(len(sorted_cats)), [c[1] for c in sorted_cats],
                    color=colors_cat[:len(sorted_cats)], edgecolor=CLR_BG, height=0.65)
    for bar, (cat, val) in zip(bars, sorted_cats):
        ax2.text(bar.get_width() + 0.01, bar.get_y() + bar.get_height()/2.,
                 f'{val:.3f}', ha='left', va='center', fontsize=9, fontweight='bold')
    ax2.set_yticks(range(len(sorted_cats)))
    ax2.set_yticklabels([c[0] for c in sorted_cats], fontsize=10)
    ax2.invert_yaxis(); ax2.set_xlabel("Total Weighted Contribution")
    ax2.set_title("Reward Category Impact", fontsize=12, fontweight='bold', pad=15)
    ax2.grid(axis='x', alpha=0.2)

    fig.tight_layout()
    fig.savefig(output_dir / "06_scaling_and_categories.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig7_variance(plt, np, names, activations, means, variances, weights, output_dir):
    fig, ax = plt.subplots(figsize=(16, 8))
    cv_data = []
    for i in range(len(names)):
        mean = abs(means[i])
        var = variances[i]
        if mean > 1e-7 and var > 0:
            cv = (var ** 0.5) / mean
            cv_data.append((_short_name(names[i]), cv, activations[i], weights[i]))
    cv_data.sort(key=lambda x: x[1], reverse=True)
    top = cv_data[:20]

    y = np.arange(len(top))
    cv_vals = [t[1] for t in top]
    colors = [CLR_WARN if v > 5 else CLR_ACCENT if v > 2 else CLR_MED for v in cv_vals]
    bars = ax.barh(y, cv_vals, color=colors, edgecolor=CLR_BG, height=0.7)
    for i, (bar, item) in enumerate(zip(bars, top)):
        w = bar.get_width()
        ax.text(w + 0.1, bar.get_y() + bar.get_height()/2.,
                f'act={item[2]:.1%}  w={item[3]:.0f}', ha='left', va='center', fontsize=8, color=CLR_LIGHT)

    ax.set_yticks(y); ax.set_yticklabels([t[0] for t in top], fontsize=9); ax.invert_yaxis()
    ax.set_xlabel("Coefficient of Variation (higher = noisier)")
    ax.set_title("Top 20 Noisiest Rewards", fontsize=14, fontweight='bold', pad=15)
    ax.axvline(x=2, color=CLR_ACCENT, linewidth=1, linestyle='--', alpha=0.5, label='CV=2')
    ax.axvline(x=5, color=CLR_WARN, linewidth=1, linestyle='--', alpha=0.5, label='CV=5')
    ax.legend(fontsize=9, frameon=False, loc='lower right')
    ax.grid(axis='x', alpha=0.2)
    fig.tight_layout()
    fig.savefig(output_dir / "07_variance_analysis.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig8_mean_changes(plt, np, common_names, curr_rewards, prev_rewards, output_dir):
    fig, ax = plt.subplots(figsize=(16, 9))
    changes = []
    for n in common_names:
        m_curr = curr_rewards[n]["mean_value"]
        m_prev = prev_rewards[n]["mean_value"]
        if abs(m_prev) > 1e-8 or abs(m_curr) > 1e-8:
            pct = ((m_curr - m_prev) / abs(m_prev) * 100) if abs(m_prev) > 1e-8 else 0
            changes.append((n, m_prev, m_curr, pct, curr_rewards[n]["weight"]))
    changes.sort(key=lambda x: abs(x[3]), reverse=True)
    top = changes[:20]

    y = np.arange(len(top))
    pcts = [max(min(t[3], 100), -100) for t in top]
    colors = [CLR_GOOD if v > 0 else CLR_WARN for v in pcts]
    bars = ax.barh(y, pcts, color=colors, edgecolor=CLR_BG, height=0.7)
    for i, (bar, item) in enumerate(zip(bars, top)):
        w = bar.get_width()
        ax.text(max(w, 0) + 2, bar.get_y() + bar.get_height()/2.,
                f'{item[1]:.5f} -> {item[2]:.5f} (w={item[4]:.0f})',
                ha='left', va='center', fontsize=7.5, color=CLR_LIGHT)

    ax.set_yticks(y); ax.set_yticklabels([_short_name(t[0]) for t in top], fontsize=9)
    ax.invert_yaxis(); ax.axvline(x=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
    ax.set_xlabel("% Change in Mean (clamped +/-100%)")
    ax.set_title("Top 20 Mean Value Changes", fontsize=14, fontweight='bold', pad=15)
    ax.grid(axis='x', alpha=0.2)
    fig.tight_layout()
    fig.savefig(output_dir / "08_mean_value_changes.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig9_skill_trends(plt, np, entries, output_dir):
    """Multi-checkpoint skill rating trend lines."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10), gridspec_kw={'height_ratios': [3, 2]})
    fig.suptitle("Skill Rating Trends Over Time", fontsize=14, fontweight='bold', color=CLR_ACCENT, y=0.98)

    timesteps = [e["total_timesteps"] / 1e9 for e in entries]
    modes = sorted(entries[0].get("skill_ratings", {}).keys())
    mode_colors = {"1v1": CLR_ACCENT, "2v2": CLR_ACCENT2, "3v3": CLR_MED}

    # Top: Absolute ratings
    for mode in modes:
        ratings = [e.get("skill_ratings", {}).get(mode, 0) for e in entries]
        ax1.plot(timesteps, ratings, 'o-', label=mode, color=mode_colors.get(mode, CLR_TEXT),
                 linewidth=2, markersize=6)
    ax1.set_ylabel("ELO Rating"); ax1.legend(fontsize=10, frameon=False)
    ax1.set_title("Absolute Skill Ratings", fontsize=12, pad=5)
    ax1.grid(alpha=0.2)

    # Bottom: Deltas
    for mode in modes:
        deltas = [e.get("skill_rating_deltas", {}).get(mode, 0) for e in entries]
        ax2.bar([t + modes.index(mode) * 0.003 for t in timesteps], deltas,
                width=0.005, label=mode, color=mode_colors.get(mode, CLR_TEXT), alpha=0.8)
    ax2.axhline(y=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
    ax2.set_xlabel("Total Timesteps (Billions)"); ax2.set_ylabel("ELO Delta per CP")
    ax2.set_title("Per-Checkpoint Rating Change", fontsize=12, pad=5)
    ax2.legend(fontsize=10, frameon=False); ax2.grid(alpha=0.2)

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(output_dir / "09_skill_rating_trends.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


def _fig10_health_trends(plt, np, entries, output_dir):
    """Reward health counts over time."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10), gridspec_kw={'height_ratios': [3, 2]})
    fig.suptitle("Reward Health Trends Over Time", fontsize=14, fontweight='bold', color=CLR_ACCENT, y=0.98)

    timesteps = [e["total_timesteps"] / 1e9 for e in entries]
    total_counts = [e.get("reward_count", 0) for e in entries]
    active_counts = [e.get("active_count", 0) for e in entries]
    dead_counts = [e.get("dead_count", 0) for e in entries]
    low_counts = [e.get("low_activation_count", 0) for e in entries]
    healthy_counts = [e.get("healthy_count", 0) for e in entries]
    problematic_counts = [e.get("problematic_count", 0) for e in entries]

    # Top: Reward counts
    ax1.plot(timesteps, total_counts, 'o-', label='Total', color=CLR_TEXT, linewidth=2, markersize=6)
    ax1.plot(timesteps, active_counts, 's-', label='Active (>0.1%)', color=CLR_GOOD, linewidth=2, markersize=6)
    ax1.plot(timesteps, dead_counts, 'D-', label='Dead (0%)', color=CLR_WARN, linewidth=2, markersize=6)
    ax1.plot(timesteps, low_counts, '^-', label='Low (<5%)', color=CLR_ACCENT, linewidth=2, markersize=6)
    ax1.axhline(y=35, color="#FCA311", linewidth=1, linestyle='--', alpha=0.6, label='Dilution threshold (35)')
    ax1.set_ylabel("Count"); ax1.legend(fontsize=9, frameon=False, loc='upper right')
    ax1.set_title("Reward Counts Over Time", fontsize=12, pad=5)
    ax1.grid(alpha=0.2)

    # Mark config changes with vertical lines
    for i in range(1, len(total_counts)):
        if total_counts[i] != total_counts[i-1]:
            ax1.axvline(x=timesteps[i], color=CLR_ACCENT, linewidth=1.5, linestyle=':', alpha=0.7)
            ax1.text(timesteps[i], ax1.get_ylim()[1] * 0.95,
                     f'{total_counts[i-1]}->{total_counts[i]}', fontsize=8,
                     ha='center', color=CLR_ACCENT, fontweight='bold')

    # Bottom: Healthy vs Problematic
    ax2.fill_between(timesteps, 0, healthy_counts, alpha=0.4, color=CLR_GOOD, label='Healthy')
    ax2.fill_between(timesteps, healthy_counts,
                     [h + p for h, p in zip(healthy_counts, problematic_counts)],
                     alpha=0.4, color=CLR_WARN, label='Problematic')
    ax2.plot(timesteps, healthy_counts, 'o-', color=CLR_GOOD, linewidth=2, markersize=5)
    ax2.plot(timesteps, [h + p for h, p in zip(healthy_counts, problematic_counts)],
             'o-', color=CLR_WARN, linewidth=2, markersize=5)
    ax2.set_xlabel("Total Timesteps (Billions)"); ax2.set_ylabel("Count")
    ax2.set_title("Healthy vs Problematic Rewards", fontsize=12, pad=5)
    ax2.legend(fontsize=9, frameon=False); ax2.grid(alpha=0.2)

    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(output_dir / "10_reward_health_trends.png", dpi=150, bbox_inches='tight')
    plt.close(fig)


# ═══════════════════════════════════════════════════════════════════════════════
# STANDALONE TESTING
# ═══════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    """Run analysis on existing checkpoints for testing."""
    import sys

    checkpoints_root = Path(r"C:\Projects\RLGymBot_v3\GigaLearnBot\build\checkpoints")
    if len(sys.argv) > 1:
        checkpoints_root = Path(sys.argv[1])

    # Find the latest checkpoint
    candidates = []
    for entry in checkpoints_root.iterdir():
        if entry.is_dir() and entry.name.isdigit() and (entry / "REWARD_METRICS.json").exists():
            candidates.append((int(entry.name), entry))

    if not candidates:
        print("No checkpoints with REWARD_METRICS.json found!")
        sys.exit(1)

    candidates.sort(key=lambda x: x[0])
    latest = candidates[-1]
    print(f"Running analysis on checkpoint {latest[0]} ({len(candidates)} total with metrics)...")

    run_analysis(
        checkpoint_path=str(latest[1]),
        checkpoints_root=str(checkpoints_root),
        total_timesteps=latest[0],
        wandb_run=None  # No wandb in standalone mode
    )
