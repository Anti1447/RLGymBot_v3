"""
Reward Diagnostics Analysis Report
Analyzes REWARD_METRICS.json and REWARD_DIAGNOSIS.json from two consecutive checkpoints
to visualize reward health, activation patterns, scaling issues, and skill rating trends.
"""

import json
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
from pathlib import Path
import os

# ── Load Data ────────────────────────────────────────────────────────────────
base = Path(r"C:\Projects\RLGymBot_v3\GigaLearnBot\build\checkpoints")

cp1_dir = base / "99477877179"
cp2_dir = base / "99499465704"

with open(cp1_dir / "REWARD_METRICS.json") as f:
    m1 = json.load(f)
with open(cp2_dir / "REWARD_METRICS.json") as f:
    m2 = json.load(f)
with open(cp1_dir / "REWARD_DIAGNOSIS.json") as f:
    d1 = json.load(f)
with open(cp2_dir / "REWARD_DIAGNOSIS.json") as f:
    d2 = json.load(f)

out_dir = Path(r"C:\Projects\RLGymBot_v3\ClaudeFiles\reward_analysis_output")
out_dir.mkdir(exist_ok=True)

# ── Helper: shorten reward names ─────────────────────────────────────────────
def short_name(name, max_len=28):
    name = name.replace("Reward", "").replace("Penalty", "Pen")
    name = name.replace("MathRewardWrapper(MULTIPLY:[", "").replace("])", "")
    name = name.replace("PlayerDataEvent", "PDE")
    name = name.replace(",ShotAccuracyInBounds", "*ShotAcc")
    if len(name) > max_len:
        name = name[:max_len-2] + ".."
    return name

# ── Build reward data arrays ─────────────────────────────────────────────────
rewards_cp2 = m2["rewards"]
rewards_cp1 = m1["rewards"]
all_names = sorted(rewards_cp2.keys())

names = []
activations_cp1 = []
activations_cp2 = []
means_cp1 = []
means_cp2 = []
variances_cp2 = []
weights = []
weighted_contrib_cp2 = []

for name in all_names:
    r2 = rewards_cp2[name]
    r1 = rewards_cp1.get(name, {})
    names.append(name)
    activations_cp2.append(r2["activation_rate"])
    activations_cp1.append(r1.get("activation_rate", 0))
    means_cp2.append(r2["mean_value"])
    means_cp1.append(r1.get("mean_value", 0))
    variances_cp2.append(r2["variance"])
    weights.append(r2["weight"])
    weighted_contrib_cp2.append(abs(r2["mean_value"]) * r2["weight"])

# ── Color scheme ─────────────────────────────────────────────────────────────
CLR_ACCENT = "#FF6B35"     # Orange
CLR_ACCENT2 = "#4ECDC4"   # Teal
CLR_WARN = "#E63946"       # Red
CLR_GOOD = "#2EC4B6"       # Green-teal
CLR_DARK = "#1D3557"       # Dark blue
CLR_MED = "#457B9D"        # Medium blue
CLR_LIGHT = "#A8DADC"      # Light blue
CLR_BG = "#0D1117"         # GitHub dark
CLR_CARD = "#161B22"       # Card bg
CLR_TEXT = "#C9D1D9"       # Light gray text
CLR_GRID = "#30363D"       # Grid lines

plt.rcParams.update({
    'figure.facecolor': CLR_BG,
    'axes.facecolor': CLR_CARD,
    'axes.edgecolor': CLR_GRID,
    'axes.labelcolor': CLR_TEXT,
    'text.color': CLR_TEXT,
    'xtick.color': CLR_TEXT,
    'ytick.color': CLR_TEXT,
    'grid.color': CLR_GRID,
    'grid.alpha': 0.3,
    'font.family': 'sans-serif',
    'font.size': 10,
})


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 1: Dashboard Overview (2x2)
# ═══════════════════════════════════════════════════════════════════════════════
fig1, axes = plt.subplots(2, 2, figsize=(18, 12))
fig1.suptitle("Reward Diagnostics Dashboard — Phase 5 Diamond (~99.5B Timesteps)",
              fontsize=16, fontweight='bold', color=CLR_ACCENT, y=0.98)

# ── 1a: Diagnosis Breakdown (Donut Chart) ────────────────────────────────────
ax = axes[0, 0]
diag_types_cp2 = {}
for d in d2["diagnoses"]:
    t = d["problem_type"]
    diag_types_cp2[t] = diag_types_cp2.get(t, 0) + 1

labels_donut = list(diag_types_cp2.keys()) + ["HEALTHY"]
sizes_donut = list(diag_types_cp2.values()) + [d2["healthy_reward_count"]]
colors_donut = [CLR_WARN, CLR_ACCENT, "#FCA311", CLR_GOOD]
# Map: LOW_ACTIVATION=red, INCORRECT_SCALING=orange, GRADIENT_DILUTION=yellow, HEALTHY=green
color_map = {
    "LOW_ACTIVATION": CLR_WARN,
    "INCORRECT_SCALING": CLR_ACCENT,
    "GRADIENT_DILUTION": "#FCA311",
    "HEALTHY": CLR_GOOD
}
donut_colors = [color_map.get(l, CLR_MED) for l in labels_donut]

wedges, texts, autotexts = ax.pie(
    sizes_donut, labels=None, autopct='%1.0f%%', startangle=90,
    colors=donut_colors, pctdistance=0.82,
    wedgeprops=dict(width=0.4, edgecolor=CLR_BG, linewidth=2)
)
for t in autotexts:
    t.set_fontsize(10)
    t.set_color('white')
    t.set_fontweight('bold')

ax.legend(
    [f"{l} ({s})" for l, s in zip(labels_donut, sizes_donut)],
    loc='center', fontsize=9, frameon=False,
    labelcolor=CLR_TEXT
)
ax.set_title("Diagnosis Breakdown (Latest Checkpoint)", fontsize=12, fontweight='bold', pad=10)

# ── 1b: Skill Rating Deltas ──────────────────────────────────────────────────
ax = axes[0, 1]
modes = list(m2["skill_rating_deltas"].keys())
deltas_cp1 = [m1["skill_rating_deltas"][m] for m in modes]
deltas_cp2 = [m2["skill_rating_deltas"][m] for m in modes]
ratings_cp1 = [m1["skill_ratings"][m] for m in modes]
ratings_cp2 = [m2["skill_ratings"][m] for m in modes]

x = np.arange(len(modes))
width = 0.35
bars1 = ax.bar(x - width/2, deltas_cp1, width, label=f'CP1 ({m1["total_timesteps"]/1e9:.1f}B)',
               color=CLR_ACCENT2, edgecolor=CLR_BG, linewidth=1)
bars2 = ax.bar(x + width/2, deltas_cp2, width, label=f'CP2 ({m2["total_timesteps"]/1e9:.1f}B)',
               color=CLR_ACCENT, edgecolor=CLR_BG, linewidth=1)

# Add value labels
for bar in bars1:
    h = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., h + (0.5 if h >= 0 else -1.5),
            f'{h:+.1f}', ha='center', va='bottom' if h >= 0 else 'top', fontsize=9, fontweight='bold')
for bar in bars2:
    h = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., h + (0.5 if h >= 0 else -1.5),
            f'{h:+.1f}', ha='center', va='bottom' if h >= 0 else 'top', fontsize=9, fontweight='bold')

# Add current ratings as secondary label
for i, m in enumerate(modes):
    ax.text(i, ax.get_ylim()[0] - 3, f'Rating: {ratings_cp2[i]:.0f}',
            ha='center', fontsize=8, color=CLR_LIGHT, style='italic')

ax.axhline(y=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
ax.set_xlabel("Game Mode")
ax.set_ylabel("ELO Delta (per checkpoint interval)")
ax.set_title("Skill Rating Deltas Between Checkpoints", fontsize=12, fontweight='bold', pad=10)
ax.set_xticks(x)
ax.set_xticklabels([f"{m}" for m in modes], fontsize=11)
ax.legend(fontsize=9, frameon=False)
ax.grid(axis='y', alpha=0.2)

# ── 1c: Activation Rate Distribution (Histogram) ─────────────────────────────
ax = axes[1, 0]
act_rates = activations_cp2
bins = [0, 0.01, 0.05, 0.1, 0.25, 0.5, 0.75, 0.9, 1.01]
bin_labels = ["0%\n(Dead)", "<5%\n(Low)", "5-10%", "10-25%", "25-50%", "50-75%", "75-90%", "90-100%"]
hist_vals, _ = np.histogram(act_rates, bins=bins)
bar_colors = [CLR_WARN, CLR_WARN, CLR_ACCENT, CLR_ACCENT, CLR_MED, CLR_MED, CLR_GOOD, CLR_GOOD]

bars = ax.bar(range(len(hist_vals)), hist_vals, color=bar_colors, edgecolor=CLR_BG, linewidth=1)
for bar, val in zip(bars, hist_vals):
    if val > 0:
        ax.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 0.3,
                str(val), ha='center', va='bottom', fontsize=10, fontweight='bold')

ax.set_xticks(range(len(bin_labels)))
ax.set_xticklabels(bin_labels, fontsize=8)
ax.set_ylabel("Number of Rewards")
ax.set_title("Activation Rate Distribution (89 Total Rewards)", fontsize=12, fontweight='bold', pad=10)
ax.grid(axis='y', alpha=0.2)

# ── 1d: Summary Stats Box ────────────────────────────────────────────────────
ax = axes[1, 1]
ax.axis('off')

dead_rewards = [n for n, a in zip(names, activations_cp2) if a == 0]
low_act_rewards = [n for n, a in zip(names, activations_cp2) if 0 < a < 0.05]
always_on = [n for n, a in zip(names, activations_cp2) if a >= 0.99]
active_count = sum(1 for a in activations_cp2 if a > 0.001)

summary_text = f"""
┌─────────────────────────────────────────────┐
│  SUMMARY STATISTICS                         │
├─────────────────────────────────────────────┤
│  Total Rewards:          89                 │
│  Active Rewards:         {active_count} (threshold: >35)    │
│  Dead Rewards (0%):      {len(dead_rewards)}                     │
│  Low Activation (<5%):   {len(low_act_rewards)}                    │
│  Always-On (≥99%):       {len(always_on)}                    │
│  Healthy Rewards:        {d2['healthy_reward_count']}                     │
│  Problematic Rewards:    {d2['problematic_reward_count']}                    │
├─────────────────────────────────────────────┤
│  SKILL RATINGS (Latest)                     │
│  1v1: {ratings_cp2[0]:7.1f}  (Δ {deltas_cp2[0]:+.1f})            │
│  2v2: {ratings_cp2[1]:7.1f}  (Δ {deltas_cp2[1]:+.1f})           │
│  3v3: {ratings_cp2[2]:7.1f}  (Δ {deltas_cp2[2]:+.1f})             │
├─────────────────────────────────────────────┤
│  DEAD REWARDS                               │
│  • ForwardPassReward                        │
│  • HighPassReward                           │
│  • SoloDefenderBumpReward                   │
├─────────────────────────────────────────────┤
│  Samples per checkpoint: {m2['rewards']['GoalReward']['sample_count']}                │
│  Timestep range: 99.48B → 99.50B            │
└─────────────────────────────────────────────┘
"""
ax.text(0.05, 0.95, summary_text, transform=ax.transAxes, fontsize=9,
        verticalalignment='top', fontfamily='monospace',
        bbox=dict(boxstyle='round,pad=0.5', facecolor=CLR_CARD, edgecolor=CLR_GRID, alpha=0.9))

fig1.tight_layout(rect=[0, 0, 1, 0.96])
fig1.savefig(out_dir / "01_dashboard_overview.png", dpi=150, bbox_inches='tight')
print("Saved: 01_dashboard_overview.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 2: Top 20 Rewards by Weighted Contribution (weight × |mean|)
# ═══════════════════════════════════════════════════════════════════════════════
fig2, ax = plt.subplots(figsize=(16, 10))

# Sort by weighted contribution
sorted_idx = np.argsort(weighted_contrib_cp2)[::-1][:20]
top_names = [short_name(names[i]) for i in sorted_idx]
top_contribs = [weighted_contrib_cp2[i] for i in sorted_idx]
top_weights = [weights[i] for i in sorted_idx]
top_means = [means_cp2[i] for i in sorted_idx]
top_acts = [activations_cp2[i] for i in sorted_idx]

y = np.arange(len(top_names))
colors_bar = []
for i in sorted_idx:
    if activations_cp2[i] >= 0.9:
        colors_bar.append(CLR_GOOD)
    elif activations_cp2[i] >= 0.3:
        colors_bar.append(CLR_MED)
    elif activations_cp2[i] >= 0.05:
        colors_bar.append(CLR_ACCENT)
    else:
        colors_bar.append(CLR_WARN)

bars = ax.barh(y, top_contribs, color=colors_bar, edgecolor=CLR_BG, linewidth=1, height=0.7)

# Annotate with details
for i, (bar, idx) in enumerate(zip(bars, sorted_idx)):
    w = bar.get_width()
    ax.text(w + 0.02, bar.get_y() + bar.get_height()/2.,
            f'w={top_weights[i]:.0f}  μ={top_means[i]:.4f}  act={top_acts[i]:.0%}',
            ha='left', va='center', fontsize=8, color=CLR_LIGHT)

ax.set_yticks(y)
ax.set_yticklabels(top_names, fontsize=9)
ax.invert_yaxis()
ax.set_xlabel("Weighted Contribution (weight × |mean_value|)", fontsize=11)
ax.set_title("Top 20 Rewards by Gradient Influence (weight × |mean|)", fontsize=14, fontweight='bold', pad=15)
ax.grid(axis='x', alpha=0.2)

# Legend for activation colors
from matplotlib.patches import Patch
legend_elements = [
    Patch(facecolor=CLR_GOOD, label='Always-On (≥90%)'),
    Patch(facecolor=CLR_MED, label='Moderate (30-90%)'),
    Patch(facecolor=CLR_ACCENT, label='Low (5-30%)'),
    Patch(facecolor=CLR_WARN, label='Very Low (<5%)'),
]
ax.legend(handles=legend_elements, loc='lower right', fontsize=9, frameon=False, title='Activation Rate')

fig2.tight_layout()
fig2.savefig(out_dir / "02_top20_weighted_contribution.png", dpi=150, bbox_inches='tight')
print("Saved: 02_top20_weighted_contribution.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 3: Weight vs Activation Scatter (INCORRECT_SCALING detection)
# ═══════════════════════════════════════════════════════════════════════════════
fig3, ax = plt.subplots(figsize=(14, 10))

# Get diagnosis info
incorrect_scaling_names = set()
for d in d2["diagnoses"]:
    if d["problem_type"] == "INCORRECT_SCALING":
        incorrect_scaling_names.add(d["reward_name"])

scatter_colors = []
scatter_sizes = []
for i, name in enumerate(names):
    if name in incorrect_scaling_names:
        scatter_colors.append(CLR_WARN)
        scatter_sizes.append(120)
    elif activations_cp2[i] == 0:
        scatter_colors.append("#555555")
        scatter_sizes.append(80)
    else:
        scatter_colors.append(CLR_ACCENT2)
        scatter_sizes.append(50)

ax.scatter(activations_cp2, weights, c=scatter_colors, s=scatter_sizes, alpha=0.7, edgecolors=CLR_BG, linewidth=0.5)

# Label the problematic ones
for i, name in enumerate(names):
    if name in incorrect_scaling_names or activations_cp2[i] == 0:
        ax.annotate(short_name(name, 22), (activations_cp2[i], weights[i]),
                    textcoords="offset points", xytext=(8, 5), fontsize=7.5,
                    color=CLR_WARN if name in incorrect_scaling_names else "#888888",
                    arrowprops=dict(arrowstyle='-', color=CLR_GRID, lw=0.5))

# Highlight the danger zone
ax.axvspan(0, 0.05, alpha=0.08, color=CLR_WARN, label='Low activation zone (<5%)')
ax.axhline(y=35, color=CLR_ACCENT, linewidth=1, linestyle='--', alpha=0.5, label='High weight threshold')

legend_elements = [
    Patch(facecolor=CLR_WARN, label='INCORRECT_SCALING diagnosis'),
    Patch(facecolor="#555555", label='Dead (0% activation)'),
    Patch(facecolor=CLR_ACCENT2, label='Normal'),
]
ax.legend(handles=legend_elements, loc='upper right', fontsize=9, frameon=False)

ax.set_xlabel("Activation Rate", fontsize=11)
ax.set_ylabel("Weight", fontsize=11)
ax.set_title("Weight vs Activation Rate — Identifying Scaling Mismatches", fontsize=14, fontweight='bold', pad=15)
ax.grid(alpha=0.2)
ax.set_xlim(-0.03, 1.05)

fig3.tight_layout()
fig3.savefig(out_dir / "03_weight_vs_activation.png", dpi=150, bbox_inches='tight')
print("Saved: 03_weight_vs_activation.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 4: Activation Rate Comparison Between Checkpoints
# ═══════════════════════════════════════════════════════════════════════════════
fig4, ax = plt.subplots(figsize=(16, 10))

# Compute change in activation
act_changes = [activations_cp2[i] - activations_cp1[i] for i in range(len(names))]
# Sort by absolute change, show top 25 changers
sorted_change_idx = sorted(range(len(names)), key=lambda i: abs(act_changes[i]), reverse=True)[:25]

change_names = [short_name(names[i]) for i in sorted_change_idx]
change_vals = [act_changes[i] for i in sorted_change_idx]
change_colors = [CLR_GOOD if v > 0 else CLR_WARN for v in change_vals]

y = np.arange(len(change_names))
bars = ax.barh(y, [v * 100 for v in change_vals], color=change_colors, edgecolor=CLR_BG, linewidth=1, height=0.7)

for i, idx in enumerate(sorted_change_idx):
    bar = bars[i]
    w = bar.get_width()
    ax.text(w + (0.3 if w >= 0 else -0.3), bar.get_y() + bar.get_height()/2.,
            f'{activations_cp1[idx]:.1%} → {activations_cp2[idx]:.1%}',
            ha='left' if w >= 0 else 'right', va='center', fontsize=8, color=CLR_LIGHT)

ax.set_yticks(y)
ax.set_yticklabels(change_names, fontsize=9)
ax.invert_yaxis()
ax.axvline(x=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
ax.set_xlabel("Change in Activation Rate (percentage points)", fontsize=11)
ax.set_title("Top 25 Activation Rate Changes Between Checkpoints", fontsize=14, fontweight='bold', pad=15)
ax.grid(axis='x', alpha=0.2)

fig4.tight_layout()
fig4.savefig(out_dir / "04_activation_changes.png", dpi=150, bbox_inches='tight')
print("Saved: 04_activation_changes.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 5: Full Activation Heatmap (all 89 rewards)
# ═══════════════════════════════════════════════════════════════════════════════
fig5, ax = plt.subplots(figsize=(18, 16))

# Sort rewards by activation rate in cp2
sorted_by_act = sorted(range(len(names)), key=lambda i: activations_cp2[i])
heatmap_names = [short_name(names[i], 30) for i in sorted_by_act]
heatmap_data = np.array([[activations_cp1[i], activations_cp2[i]] for i in sorted_by_act])

im = ax.imshow(heatmap_data, aspect='auto', cmap='RdYlGn', vmin=0, vmax=1)
ax.set_xticks([0, 1])
ax.set_xticklabels([f'CP1\n({m1["total_timesteps"]/1e9:.2f}B)', f'CP2\n({m2["total_timesteps"]/1e9:.2f}B)'], fontsize=10)
ax.set_yticks(range(len(heatmap_names)))
ax.set_yticklabels(heatmap_names, fontsize=6.5)
ax.set_title("Activation Rate Heatmap — All 89 Rewards (Sorted by Latest)", fontsize=14, fontweight='bold', pad=15)

# Add text annotations for low activation
for i in range(len(sorted_by_act)):
    for j in range(2):
        val = heatmap_data[i, j]
        if val < 0.3:
            ax.text(j, i, f'{val:.1%}', ha='center', va='center', fontsize=5.5,
                    color='white' if val < 0.15 else 'black', fontweight='bold')

cbar = plt.colorbar(im, ax=ax, shrink=0.6, label='Activation Rate')
cbar.ax.yaxis.label.set_color(CLR_TEXT)
cbar.ax.tick_params(colors=CLR_TEXT)

fig5.tight_layout()
fig5.savefig(out_dir / "05_activation_heatmap.png", dpi=150, bbox_inches='tight')
print("Saved: 05_activation_heatmap.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 6: INCORRECT_SCALING Detail — Weight vs Contribution for flagged rewards
# ═══════════════════════════════════════════════════════════════════════════════
fig6, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))

# Left: Flagged rewards detail
scaling_diag = [d for d in d2["diagnoses"] if d["problem_type"] == "INCORRECT_SCALING"]
scaling_names = [short_name(d["reward_name"], 24) for d in scaling_diag]
scaling_weights_vals = []
scaling_contribs = []
for d in scaling_diag:
    rname = d["reward_name"]
    r = rewards_cp2[rname]
    scaling_weights_vals.append(r["weight"])
    scaling_contribs.append(abs(r["mean_value"]) * r["weight"])

y = np.arange(len(scaling_names))
ax1.barh(y - 0.2, scaling_weights_vals, 0.4, label='Weight', color=CLR_ACCENT, edgecolor=CLR_BG)
ax1_twin = ax1.twiny()
ax1_twin.barh(y + 0.2, scaling_contribs, 0.4, label='Weighted Contrib', color=CLR_ACCENT2, edgecolor=CLR_BG)

ax1.set_yticks(y)
ax1.set_yticklabels(scaling_names, fontsize=9)
ax1.set_xlabel("Weight", color=CLR_ACCENT, fontsize=10)
ax1_twin.set_xlabel("Weighted Contribution (w × |μ|)", color=CLR_ACCENT2, fontsize=10)
ax1_twin.tick_params(axis='x', colors=CLR_ACCENT2)
ax1.invert_yaxis()
ax1.set_title("INCORRECT_SCALING Rewards\nHigh Weight but Low Impact", fontsize=12, fontweight='bold', pad=15)
ax1.grid(axis='x', alpha=0.2)

# Right: Reward category breakdown
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
    "Flicks": ["BackFlipFlick", "AdvancedFlick", "FlickExploration", "MustyFlick",
               "MathRewardWrapper(MULTIPLY:[FlickOnGoal"],
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
                total += weighted_contrib_cp2[i]
                break
    cat_contribs[cat] = total

sorted_cats = sorted(cat_contribs.items(), key=lambda x: x[1], reverse=True)
cat_names_sorted = [c[0] for c in sorted_cats]
cat_vals_sorted = [c[1] for c in sorted_cats]

colors_cat = [CLR_ACCENT2, CLR_MED, CLR_ACCENT, CLR_DARK, CLR_GOOD, CLR_LIGHT, "#FCA311", CLR_WARN]
bars = ax2.barh(range(len(cat_names_sorted)), cat_vals_sorted, color=colors_cat[:len(cat_names_sorted)],
                edgecolor=CLR_BG, linewidth=1, height=0.65)

for bar, val in zip(bars, cat_vals_sorted):
    ax2.text(bar.get_width() + 0.01, bar.get_y() + bar.get_height()/2.,
             f'{val:.3f}', ha='left', va='center', fontsize=9, fontweight='bold')

ax2.set_yticks(range(len(cat_names_sorted)))
ax2.set_yticklabels(cat_names_sorted, fontsize=10)
ax2.invert_yaxis()
ax2.set_xlabel("Total Weighted Contribution", fontsize=10)
ax2.set_title("Reward Category Impact Distribution", fontsize=12, fontweight='bold', pad=15)
ax2.grid(axis='x', alpha=0.2)

fig6.tight_layout()
fig6.savefig(out_dir / "06_scaling_and_categories.png", dpi=150, bbox_inches='tight')
print("Saved: 06_scaling_and_categories.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 7: Variance Analysis (which rewards are noisy?)
# ═══════════════════════════════════════════════════════════════════════════════
fig7, ax = plt.subplots(figsize=(16, 8))

# Coefficient of variation (std / |mean|) where mean != 0
cv_data = []
for i in range(len(names)):
    mean = abs(means_cp2[i])
    var = variances_cp2[i]
    if mean > 1e-7 and var > 0:
        std = np.sqrt(var)
        cv = std / mean
        cv_data.append((names[i], cv, activations_cp2[i], weights[i]))

cv_data.sort(key=lambda x: x[1], reverse=True)
top_cv = cv_data[:20]

cv_names = [short_name(t[0]) for t in top_cv]
cv_vals = [t[1] for t in top_cv]
cv_acts = [t[2] for t in top_cv]
cv_colors = [CLR_WARN if v > 5 else CLR_ACCENT if v > 2 else CLR_MED for v in cv_vals]

y = np.arange(len(cv_names))
bars = ax.barh(y, cv_vals, color=cv_colors, edgecolor=CLR_BG, linewidth=1, height=0.7)

for i, (bar, cv_item) in enumerate(zip(bars, top_cv)):
    w = bar.get_width()
    ax.text(w + 0.1, bar.get_y() + bar.get_height()/2.,
            f'act={cv_item[2]:.1%}  w={cv_item[3]:.0f}',
            ha='left', va='center', fontsize=8, color=CLR_LIGHT)

ax.set_yticks(y)
ax.set_yticklabels(cv_names, fontsize=9)
ax.invert_yaxis()
ax.set_xlabel("Coefficient of Variation (σ / |μ|) — higher = noisier signal", fontsize=11)
ax.set_title("Top 20 Noisiest Rewards (Coefficient of Variation)", fontsize=14, fontweight='bold', pad=15)
ax.axvline(x=2, color=CLR_ACCENT, linewidth=1, linestyle='--', alpha=0.5, label='CV=2 (noisy threshold)')
ax.axvline(x=5, color=CLR_WARN, linewidth=1, linestyle='--', alpha=0.5, label='CV=5 (very noisy)')
ax.legend(fontsize=9, frameon=False, loc='lower right')
ax.grid(axis='x', alpha=0.2)

fig7.tight_layout()
fig7.savefig(out_dir / "07_variance_analysis.png", dpi=150, bbox_inches='tight')
print("Saved: 07_variance_analysis.png")


# ═══════════════════════════════════════════════════════════════════════════════
# FIGURE 8: Mean Value Change Between Checkpoints (top movers)
# ═══════════════════════════════════════════════════════════════════════════════
fig8, ax = plt.subplots(figsize=(16, 9))

mean_changes = []
for i in range(len(names)):
    if abs(means_cp1[i]) > 1e-8 or abs(means_cp2[i]) > 1e-8:
        pct_change = 0
        if abs(means_cp1[i]) > 1e-8:
            pct_change = (means_cp2[i] - means_cp1[i]) / abs(means_cp1[i]) * 100
        mean_changes.append((names[i], means_cp1[i], means_cp2[i], pct_change, weights[i]))

mean_changes.sort(key=lambda x: abs(x[3]), reverse=True)
top_mean = mean_changes[:20]

mc_names = [short_name(t[0]) for t in top_mean]
mc_pcts = [t[3] for t in top_mean]
mc_colors = [CLR_GOOD if v > 0 else CLR_WARN for v in mc_pcts]

y = np.arange(len(mc_names))
# Clamp display to ±100% for readability
mc_pcts_clamped = [max(min(v, 100), -100) for v in mc_pcts]
bars = ax.barh(y, mc_pcts_clamped, color=mc_colors, edgecolor=CLR_BG, linewidth=1, height=0.7)

for i, (bar, item) in enumerate(zip(bars, top_mean)):
    w = bar.get_width()
    label = f'{item[1]:.5f} → {item[2]:.5f} (w={item[4]:.0f})'
    ax.text(max(w, 0) + 2, bar.get_y() + bar.get_height()/2.,
            label, ha='left', va='center', fontsize=7.5, color=CLR_LIGHT)

ax.set_yticks(y)
ax.set_yticklabels(mc_names, fontsize=9)
ax.invert_yaxis()
ax.axvline(x=0, color=CLR_TEXT, linewidth=0.8, alpha=0.5)
ax.set_xlabel("% Change in Mean Value (clamped to ±100%)", fontsize=11)
ax.set_title("Top 20 Mean Value Changes Between Checkpoints", fontsize=14, fontweight='bold', pad=15)
ax.grid(axis='x', alpha=0.2)

fig8.tight_layout()
fig8.savefig(out_dir / "08_mean_value_changes.png", dpi=150, bbox_inches='tight')
print("Saved: 08_mean_value_changes.png")


plt.close('all')
print(f"\nAll figures saved to: {out_dir}")
print("Done!")
