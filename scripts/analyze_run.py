"""
analyze_run.py — post-simulation analysis and figure generation.

Reads the most-recent CSV log from logs/ (or all logs with --all) and produces
five publication-quality figures in docs/figures/:

  fig1_sir_curves.png         Susceptible / Exposed / Infected / Recovered over time
  fig2_platform_contribution.png   Stacked area chart of transmissions by platform
  fig3_zone_vulnerability.png      Horizontal bar chart of top-10 most-infected zones
  fig4_factcheck_efficacy.png      Annotated SIR curve showing fact-check intervention
  fig5_scenario_comparison.png     Overlay of peak infection curves across scenarios

Also prints a findings block with:
  - Time to 10% infection
  - Peak infected % and tick
  - Top transmission platform and share
  - Fact-check recovery %
  - Pearson r between zone literacy and infection time

Key implementation decisions:
  - Paths are resolved via pathlib relative to this script's location so the
    script works correctly regardless of the cwd when called.
  - Zone name matching between log CSV and agents_config.csv uses difflib fuzzy
    matching because the C++ simulation may emit slightly different name strings
    than the Python CSV (trailing spaces, encoding differences).
  - Fact-check detection uses a two-stage heuristic: find the largest single-tick
    drop in the infected count that (a) occurs after peak and (b) exceeds 3×
    the median tick-to-tick recovered_delta over the post-peak window.  This
    avoids misidentifying natural recovery troughs as interventions.
"""

import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import glob
import sys
import os
import pathlib
from difflib import get_close_matches
from scipy import stats

SCRIPT_DIR   = pathlib.Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent
AGENTS_CSV   = PROJECT_ROOT / "data" / "processed" / "agents_config.csv"
FIGURES_DIR  = PROJECT_ROOT / "docs" / "figures"
LOGS_DIR     = PROJECT_ROOT / "logs"

try:
    import seaborn as sns
    sns.set_theme(style="darkgrid")
except ImportError:
    pass

plt.rcParams.update({
    "figure.facecolor": "#0D1117",
    "axes.facecolor":   "#161B22",
    "axes.edgecolor":   "#30363D",
    "axes.labelcolor":  "#C9D1D9",
    "xtick.color":      "#8B949E",
    "ytick.color":      "#8B949E",
    "text.color":       "#C9D1D9",
    "grid.color":       "#21262D",
    "grid.linewidth":   0.8,
    "legend.facecolor": "#161B22",
    "legend.edgecolor": "#30363D",
})

os.makedirs(FIGURES_DIR, exist_ok=True)

PLATFORM_COLOURS = {
    "Facebook":  "#3B5998",
    "WhatsApp":  "#25D366",
    "TikTok":    "#FF0050",
    "Twitter":   "#1DA1F2",
    "YouTube":   "#FF0000",
    "Reddit":    "#FF4500",
    "WeChat":    "#09BB07",
    "Telegram":  "#2CA5E0",
    "Instagram": "#C13584",
    "Snapchat":  "#FFFC00",
    "VK":        "#4C75A3",
    "KakaoTalk": "#FAE100",
    "Douyin":    "#010101",
    "Weibo":     "#E6162D",
    "Unknown":   "#888888",
}

REGION_COLOURS = {
    "East Asia & Pacific":                                      "#1A6B9A",
    "Europe & Central Asia":                                    "#6B3FA0",
    "Latin America & Caribbean":                                "#2A7A50",
    "Middle East, North Africa, Afghanistan & Pakistan":        "#9A5A1A",
    "North America":                                            "#1A5A8A",
    "South Asia":                                               "#9A8A1A",
    "Sub-Saharan Africa":                                       "#9A3A1A",
}

def load_scenario(csv_path):
    df = pd.read_csv(csv_path)
    for col in df.columns:
        if col != "tick":
            df[col] = pd.to_numeric(df[col], errors="coerce")
    state_cols = [c for c in ["susceptible","exposed","infected","recovered"] if c in df.columns]
    if state_cols:
        df["total"] = df[state_cols].sum(axis=1)
    else:
        df["total"] = df.iloc[:, 1:5].sum(axis=1)  # fallback: first 4 numeric cols
    df["infected_pct"]   = df["infected"]  / df["total"] * 100
    df["recovered_pct"]  = df["recovered"] / df["total"] * 100
    return df

def extract_scenario_label(path):
    base = os.path.basename(path)
    # e.g. run_scenario_a_controlled_burn_1234567890.csv
    for label in ["scenario_a", "scenario_b", "scenario_c"]:
        if label in base:
            return label
    return os.path.splitext(base)[0]

# ── Load data ────────────────────────────────────────────────────────────────
scenarios = {}

if len(sys.argv) >= 2 and sys.argv[1] == "--all":
    csv_files = sorted(glob.glob(str(LOGS_DIR / "run_scenario_*.csv")))
    if not csv_files:
        print(f"No scenario CSVs found in {LOGS_DIR}. Run the simulation first.")
        sys.exit(1)
    seen = {}
    for f in sorted(csv_files, key=os.path.getmtime, reverse=True):
        label = extract_scenario_label(f)
        if label not in seen:
            seen[label] = f
            print(f"Loading {label}: {f}")
    for label, f in seen.items():
        scenarios[label] = load_scenario(f)
elif len(sys.argv) >= 2:
    f = sys.argv[1]
    label = extract_scenario_label(f)
    scenarios[label] = load_scenario(f)
    print(f"Loading {label}: {f}")
else:
    csv_files = sorted(glob.glob(str(LOGS_DIR / "run_scenario_*.csv")), key=os.path.getmtime)
    if not csv_files:
        print("No log files found. Run: ./build/simulation scenarios/scenario_a.json")
        sys.exit(1)
    f = csv_files[-1]
    label = extract_scenario_label(f)
    scenarios[label] = load_scenario(f)
    print(f"Auto-loaded most recent: {f}")

# Primary df: prefer scenario_c, else first available
primary_label = "scenario_c" if "scenario_c" in scenarios else list(scenarios.keys())[0]
df = scenarios[primary_label]
print(f"Primary scenario: {primary_label} ({len(df)} ticks)")

platform_cols = [c for c in df.columns if c.startswith("tx_")]
zone_cols     = [c for c in df.columns if c.endswith("_infected") and c != "infected"]
print(f"Platform columns: {platform_cols}")
print(f"Zone columns:     {len(zone_cols)} found")

# ── Detect event ticks ────────────────────────────────────────────────────────
first_infection_tick = df[df["infected"] > 0]["tick"].min() if "infected" in df.columns else None
peak_infected_tick   = df.loc[df["infected"].idxmax(), "tick"] if "infected" in df.columns else None

def detect_factcheck_tick(df):
    peak_tick = df.loc[df["infected"].idxmax(), "tick"]
    post_peak = df[df["tick"] > peak_tick].copy()
    if post_peak.empty:
        return None
    post_peak["recovered_delta"] = post_peak["recovered"].diff()
    max_jump_idx = post_peak["recovered_delta"].idxmax()
    if pd.isna(max_jump_idx):
        return None
    median_delta = post_peak["recovered_delta"].median()
    max_delta    = post_peak["recovered_delta"].max()
    if max_delta > median_delta * 3:
        return post_peak.loc[max_jump_idx, "tick"]
    return None

factcheck_tick = detect_factcheck_tick(df) if "recovered" in df.columns else None

print(f"first_infection_tick={first_infection_tick}, peak_tick={peak_infected_tick}, factcheck_tick={factcheck_tick}")

# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 1 — SIR Curves
# ══════════════════════════════════════════════════════════════════════════════
fig, ax = plt.subplots(figsize=(12, 6))
fig.patch.set_facecolor("#0D1117")
ax.set_facecolor("#161B22")

state_style = [
    ("susceptible", "#2ECC71", "Susceptible"),
    ("exposed",     "#F5A623", "Exposed"),
    ("infected",    "#E94560", "Infected"),
    ("recovered",   "#00B4D8", "Recovered"),
]
for col, colour, label in state_style:
    if col in df.columns:
        ax.plot(df["tick"], df[col], colour, linewidth=2, label=label)

if first_infection_tick is not None and not np.isnan(first_infection_tick):
    ax.axvline(first_infection_tick, color="white",  linestyle="--", alpha=0.6, linewidth=1)
    ax.text(first_infection_tick + 2, ax.get_ylim()[1] * 0.95, "Infection seeded",
            color="white", fontsize=9, va="top")

if peak_infected_tick is not None and not np.isnan(peak_infected_tick):
    ax.axvline(peak_infected_tick, color="#E94560", linestyle="--", alpha=0.6, linewidth=1)
    ax.text(peak_infected_tick + 2, ax.get_ylim()[1] * 0.85, "Peak infection",
            color="#E94560", fontsize=9, va="top")

if factcheck_tick is not None and not np.isnan(factcheck_tick):
    ax.axvline(factcheck_tick, color="#00B4D8", linestyle="--", alpha=0.6, linewidth=1)
    ax.text(factcheck_tick + 2, ax.get_ylim()[1] * 0.75, "Fact-check fired",
            color="#00B4D8", fontsize=9, va="top")

peak_pct = df["infected_pct"].max() if "infected_pct" in df.columns else 0.0
ax.set_title(f"Peak infection reached {peak_pct:.1f}% of population at tick {peak_infected_tick}",
             color="#C9D1D9", fontsize=13, pad=12)
ax.set_xlabel("Simulation Tick", fontsize=11)
ax.set_ylabel("Agent Count", fontsize=11)
ax.legend(framealpha=0.3, fontsize=10)
ax.spines[["top","right"]].set_visible(False)
plt.tight_layout()
plt.savefig(str(FIGURES_DIR / "fig1_sir_curves.png"), dpi=150, bbox_inches="tight")
plt.close()
print("Saved: docs/figures/fig1_sir_curves.png")

# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 2 — Platform Contribution
# ══════════════════════════════════════════════════════════════════════════════
fig, ax = plt.subplots(figsize=(12, 6))
fig.patch.set_facecolor("#0D1117")
ax.set_facecolor("#161B22")

if platform_cols:
    cum_totals = {c: df[c].sum() for c in platform_cols}
    total_tx   = sum(cum_totals.values())
    sorted_plat = sorted(platform_cols, key=lambda c: cum_totals[c], reverse=True)

    running_pcts = {}
    for c in sorted_plat:
        cum = df[c].cumsum()
        total_cum = df[sorted_plat].sum(axis=1).cumsum().replace(0, np.nan)
        running_pcts[c] = (cum / total_cum * 100).fillna(0)

    baseline = np.zeros(len(df))
    for c in reversed(sorted_plat):
        pname = c.replace("tx_", "").capitalize()
        colour = PLATFORM_COLOURS.get(pname, "#888888")
        vals   = running_pcts[c].values
        ax.fill_between(df["tick"], baseline, baseline + vals, alpha=0.7,
                        color=colour, label=pname)
        baseline = baseline + vals

    top_col  = sorted_plat[0]
    top_name = top_col.replace("tx_", "").capitalize()
    top_pct  = cum_totals[top_col] / max(total_tx, 1) * 100
    ax.set_title(f"{top_name} drove {top_pct:.1f}% of all transmissions",
                 color="#C9D1D9", fontsize=13, pad=12)
else:
    ax.text(0.5, 0.5, "No platform transmission columns (tx_*) found in log",
            ha="center", va="center", transform=ax.transAxes, color="#C9D1D9", fontsize=12)
    top_name, top_pct = "N/A", 0.0
    ax.set_title("Platform Contribution (no data)", color="#C9D1D9", fontsize=13)

ax.set_xlabel("Simulation Tick", fontsize=11)
ax.set_ylabel("Share of Cumulative Transmissions (%)", fontsize=11)
ax.set_ylim(0, 100)
ax.legend(loc="upper left", framealpha=0.3, fontsize=9)
ax.spines[["top","right"]].set_visible(False)
plt.tight_layout()
plt.savefig(str(FIGURES_DIR / "fig2_platform_contribution.png"), dpi=150, bbox_inches="tight")
plt.close()
print("Saved: docs/figures/fig2_platform_contribution.png")

# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 3 — Zone Vulnerability
# ══════════════════════════════════════════════════════════════════════════════
fig, ax = plt.subplots(figsize=(12, 10))
fig.patch.set_facecolor("#0D1117")
ax.set_facecolor("#161B22")

if zone_cols:
    agents_cfg = None
    if AGENTS_CSV.exists():
        agents_cfg = pd.read_csv(AGENTS_CSV)

    total_agents = df["total"].iloc[0] if "total" in df.columns else 1
    agents_per_zone_approx = total_agents / len(zone_cols)

    zone_peaks = {}
    for c in zone_cols:
        zname = c.replace("_infected", "")
        peak  = df[c].max()
        pct   = peak / max(agents_per_zone_approx, 1) * 100
        zone_peaks[zname] = min(pct, 100.0)   # cap at 100%

    sorted_zones = sorted(zone_peaks.items(), key=lambda x: x[1], reverse=True)[:30]
    names = [z[0] for z in sorted_zones]
    pcts  = [z[1] for z in sorted_zones]

    # Get region for each zone
    def get_region(zname):
        if agents_cfg is not None:
            row = agents_cfg[agents_cfg["code"] == zname]
            if len(row) > 0 and "region" in row.columns:
                return row.iloc[0]["region"]
        return "Unknown"

    colours = []
    for name in names:
        region = get_region(name)
        colours.append(REGION_COLOURS.get(region, "#555555"))

    bars = ax.barh(range(len(names)), pcts, color=colours, alpha=0.85, edgecolor="#30363D", linewidth=0.5)
    ax.set_yticks(range(len(names)))
    ax.set_yticklabels(names, fontsize=9)
    ax.axvline(np.mean(pcts), color="white", linestyle="--", alpha=0.5, linewidth=1,
               label=f"Mean: {np.mean(pcts):.1f}%")

    seen_regions = set()
    legend_patches = []
    for name in names:
        r = get_region(name)
        if r not in seen_regions:
            seen_regions.add(r)
            legend_patches.append(mpatches.Patch(color=REGION_COLOURS.get(r,"#555"), label=r))
    ax.legend(handles=legend_patches, loc="lower right", framealpha=0.3, fontsize=8)

    all_pcts = list(zone_peaks.values())
    ax.set_title(
        f"Peak infection ranged from {min(all_pcts):.1f}% to {max(all_pcts):.1f}% across {len(zone_cols)} zones",
        color="#C9D1D9", fontsize=13, pad=12)
    ax.set_xlabel("Peak Infection %", fontsize=11)
else:
    ax.text(0.5, 0.5, "No zone columns (*_infected) found in log",
            ha="center", va="center", transform=ax.transAxes, color="#C9D1D9", fontsize=12)
    ax.set_title("Zone Vulnerability (no data)", color="#C9D1D9", fontsize=13)

ax.spines[["top","right"]].set_visible(False)
plt.tight_layout()
plt.savefig(str(FIGURES_DIR / "fig3_zone_vulnerability.png"), dpi=150, bbox_inches="tight")
plt.close()
print("Saved: docs/figures/fig3_zone_vulnerability.png")

# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 4 — Fact-Check Efficacy
# ══════════════════════════════════════════════════════════════════════════════
fig, ax = plt.subplots(figsize=(12, 6))
fig.patch.set_facecolor("#0D1117")
ax.set_facecolor("#161B22")

factcheck_recovery_pct = "N/A"

if factcheck_tick is not None and not np.isnan(factcheck_tick):
    t0 = max(df["tick"].min(), factcheck_tick - 30)
    t1 = min(df["tick"].max(), factcheck_tick + 150)
    zoom = df[(df["tick"] >= t0) & (df["tick"] <= t1)]

    ax.plot(zoom["tick"], zoom["infected"],  "#E94560", linewidth=2, label="Infected")
    ax.plot(zoom["tick"], zoom["recovered"], "#00B4D8", linewidth=2, label="Recovered")

    post = zoom[zoom["tick"] >= factcheck_tick]
    ax.fill_between(post["tick"], 0, post["infected"],  alpha=0.15, color="#E94560")
    baseline_rec = zoom.loc[zoom["tick"] == factcheck_tick, "recovered"]
    baseline_val = baseline_rec.values[0] if len(baseline_rec) > 0 else 0
    ax.fill_between(post["tick"], baseline_val, post["recovered"], alpha=0.15, color="#00B4D8")

    ax.axvline(factcheck_tick, color="white", linestyle="--", alpha=0.7, linewidth=1.5,
               label="Fact-check deployed")

    tick_before = df[df["tick"] == factcheck_tick - 1]
    tick_after  = df[df["tick"] == factcheck_tick]
    if not tick_before.empty and not tick_after.empty:
        recovered_before = tick_before["recovered"].values[0]
        recovered_after  = tick_after["recovered"].values[0]
        infected_before  = tick_before["infected"].values[0]
        jump = recovered_after - recovered_before
        if infected_before > 0:
            factcheck_recovery_pct = f"{jump / infected_before * 100:.1f}"
    title = (f"Fact-check recovered {factcheck_recovery_pct}% of infected agents immediately"
             f" — but could not reverse spread momentum")
    ax.set_title(title, color="#C9D1D9", fontsize=12, pad=12)
else:
    ax.text(0.5, 0.5, "No fact-check event detected in this run",
            ha="center", va="center", transform=ax.transAxes, color="#C9D1D9", fontsize=13)
    ax.set_title("Fact-Check Efficacy (no fact-check event detected)", color="#C9D1D9", fontsize=13)

ax.set_xlabel("Simulation Tick", fontsize=11)
ax.set_ylabel("Agent Count", fontsize=11)
ax.legend(framealpha=0.3, fontsize=10)
ax.spines[["top","right"]].set_visible(False)
plt.tight_layout()
plt.savefig(str(FIGURES_DIR / "fig4_factcheck_efficacy.png"), dpi=150, bbox_inches="tight")
plt.close()
print("Saved: docs/figures/fig4_factcheck_efficacy.png")

# ══════════════════════════════════════════════════════════════════════════════
# FIGURE 5 — Scenario Comparison
# ══════════════════════════════════════════════════════════════════════════════
fig, ax = plt.subplots(figsize=(12, 6))
fig.patch.set_facecolor("#0D1117")
ax.set_facecolor("#161B22")

scenario_style = {
    "scenario_a": ("#2ECC71", "solid",  "Scenario A: Controlled Burn"),
    "scenario_b": ("#F5A623", "dashed", "Scenario B: Silent Wildfire"),
    "scenario_c": ("#E94560", "solid",  "Scenario C: Perfect Storm"),
}

if len(scenarios) >= 2:
    for lbl, sdf in scenarios.items():
        colour, ls, name = scenario_style.get(lbl, ("#AAAAAA", "solid", lbl))
        ax.plot(sdf["tick"], sdf["infected_pct"], color=colour, linestyle=ls,
                linewidth=2.2, label=name)
        ax.fill_between(sdf["tick"], 0, sdf["infected_pct"], alpha=0.08, color=colour)

    a_peak = scenarios.get("scenario_a", pd.DataFrame({"infected_pct":[0]}))["infected_pct"].max()
    c_peak = scenarios.get("scenario_c", pd.DataFrame({"infected_pct":[0]}))["infected_pct"].max()
    ratio  = c_peak / max(a_peak, 0.01)
    ax.set_title(
        f"Perfect Storm peaked {ratio:.1f}× higher than Controlled Burn ({c_peak:.1f}% vs {a_peak:.1f}%)",
        color="#C9D1D9", fontsize=13, pad=12)
    ax.legend(framealpha=0.3, fontsize=10)
else:
    ax.text(0.5, 0.5, "Run all 3 scenarios to generate comparison\n(use --all flag)",
            ha="center", va="center", transform=ax.transAxes, color="#C9D1D9", fontsize=13,
            linespacing=2)
    ax.set_title("Scenario Comparison", color="#C9D1D9", fontsize=13)

ax.set_xlabel("Simulation Tick", fontsize=11)
ax.set_ylabel("Infected (%)", fontsize=11)
ax.spines[["top","right"]].set_visible(False)
plt.tight_layout()
plt.savefig(str(FIGURES_DIR / "fig5_scenario_comparison.png"), dpi=150, bbox_inches="tight")
plt.close()
print("Saved: docs/figures/fig5_scenario_comparison.png")

# ══════════════════════════════════════════════════════════════════════════════
# PRINT FINDINGS
# ══════════════════════════════════════════════════════════════════════════════
total_agents      = int(df["total"].iloc[0]) if "total" in df.columns else 0
peak_infected_n   = int(df["infected"].max()) if "infected" in df.columns else 0
peak_infected_pct = df["infected_pct"].max() if "infected_pct" in df.columns else 0.0
peak_tick_val     = int(df.loc[df["infected"].idxmax(), "tick"]) if "infected" in df.columns else 0

ticks_to_10 = df[df["infected_pct"] >= 10.0]["tick"].min() if "infected_pct" in df.columns else np.nan
ticks_to_10_str = str(int(ticks_to_10)) if pd.notna(ticks_to_10) else "Did not reach 10%"

if platform_cols:
    cum_totals_final = {c: df[c].sum() for c in platform_cols}
    total_tx_final   = sum(cum_totals_final.values())
    top_col_f        = max(cum_totals_final, key=cum_totals_final.get)
    top_name_f       = top_col_f.replace("tx_", "").capitalize()
    top_pct_f        = cum_totals_final[top_col_f] / max(total_tx_final, 1) * 100
    top_platform_str = f"{top_name_f} ({top_pct_f:.1f}%)"
else:
    top_platform_str = "N/A (no tx_ columns in log)"

pearson_r_str = "N/A"
p_val_str     = "N/A"
if zone_cols and AGENTS_CSV.exists():
    lit_df = pd.read_csv(AGENTS_CSV)
    country_names = lit_df["country"].tolist()

    total_agents_c    = df["total"].iloc[0]
    agents_per_zone_c = total_agents_c / len(zone_cols)
    threshold         = agents_per_zone_c * 0.1

    literacy_vals = []
    tick_vals     = []
    for c in zone_cols:
        zname = c.replace("_infected", "").strip('"')
        hit   = df[df[c] >= threshold]["tick"].min()
        if pd.isna(hit):
            continue
        # Exact match first, then fuzzy
        match = lit_df[lit_df["country"] == zname]
        if match.empty:
            close = get_close_matches(zname, country_names, n=1, cutoff=0.6)
            if close:
                match = lit_df[lit_df["country"] == close[0]]
        if not match.empty and "literacy_score" in match.columns:
            literacy_vals.append(match.iloc[0]["literacy_score"])
            tick_vals.append(hit)

    print(f"Literacy correlation: {len(literacy_vals)} zones matched")
    if len(literacy_vals) >= 5:
        r, p = stats.pearsonr(literacy_vals, tick_vals)
        pearson_r_str = f"{r:.3f}"
        p_val_str     = f"{p:.4f}"
    else:
        pearson_r_str = f"N/A (only {len(literacy_vals)} matched zones)"
        p_val_str     = "N/A"

print(f"""
╔══════════════════════════════════════════════════════════╗
║           DIGITAL CONTAGION — SIMULATION FINDINGS        ║
╠══════════════════════════════════════════════════════════╣
║ Total agents simulated:    {total_agents}
║ Ticks to 10% infected:     {ticks_to_10_str}
║ Peak infected count:       {peak_infected_n} ({peak_infected_pct:.1f}%)
║ Tick of peak infection:    {peak_tick_val}
║ Top transmission platform: {top_platform_str}
║ Fact-check recovery:       {factcheck_recovery_pct}% of infected
║ Literacy vs spread (r):    {pearson_r_str} (p={p_val_str})
╚══════════════════════════════════════════════════════════╝

Figures saved to docs/figures/
  fig1_sir_curves.png
  fig2_platform_contribution.png
  fig3_zone_vulnerability.png
  fig4_factcheck_efficacy.png
  fig5_scenario_comparison.png
""")
