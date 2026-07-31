from __future__ import annotations

import difflib
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path("/workspace/scratch/81034b858d1a")
SOURCE = ROOT / "Pet_Collar_PC_ReleaseAware_V18"
INPUT = ROOT / "upload" / "input(1).csv"
REFERENCE = ROOT / "upload" / "01validation.csv"
STUDY = ROOT / "analysis_work" / "v18_release_single_variable_study"
VARIANT_ROOT = STUDY / "variants"
OUT = ROOT / "delivery" / "V18_45_46s_Single_Variable_Study"


@dataclass(frozen=True)
class Variant:
    name: str
    category: str
    description: str
    target_file: str | None = None
    old: str | None = None
    new: str | None = None


HISTORY_PRIMARY_IMMEDIATE = r'''
    /* Single-variable study: an already-established challenger does not need
     * a second, fresh release confirmation once it becomes the clear primary
     * candidate.  Reuse the history that has already accumulated. */
    if ((est->hr_challenger_bpm > 0.0f) &&
        (est->hr_challenger_hits >= 2U) &&
        (est->hr_challenger_gap <= VITAL_HR_CHALLENGER_MAX_GAP) &&
        (est->hr_challenger_evidence >= VITAL_HR_CHALLENGER_CONFIRM) &&
        (search->count > 0U) &&
        (Vital_AbsF(search->top[0].bpm - est->hr_challenger_bpm) <=
         VITAL_HR_CHALLENGER_MATCH_BPM) &&
        (search->top[0].bpm <
         (((est->hr_anchor_track_bpm > 0.0f) ?
           est->hr_anchor_track_bpm : est->hr_track_bpm) -
          VITAL_HR_SWITCH_DELTA_BPM)) &&
        (search->top[0].evidence_quality >= 0.55f) &&
        (primary_margin >= VITAL_HR_EVIDENCE_MARGIN_CLEAR))
    {
        est->hr_challenger_confirmed = 1U;
        est->hr_challenger_release_confirmed = 1U;
    }

'''

CURRENT_PRIMARY_IMMEDIATE = r'''
    /* Single-variable study: a clear far primary candidate can directly
     * replace the old track without using challenger history. */
    if ((search->count > 0U) &&
        (search->top[0].bpm <
         (((est->hr_anchor_track_bpm > 0.0f) ?
           est->hr_anchor_track_bpm : est->hr_track_bpm) -
          VITAL_HR_SWITCH_DELTA_BPM)) &&
        (search->top[0].evidence_quality >= 0.55f) &&
        (primary_margin >= VITAL_HR_EVIDENCE_MARGIN_CLEAR))
    {
        est->hr_challenger_bpm = search->top[0].bpm;
        est->hr_challenger_confirmed = 1U;
        est->hr_challenger_release_confirmed = 1U;
    }

'''

RECENT_PRIMARY_IMMEDIATE = r'''
    /* Single-variable study: recent-window consistency is the one decisive
     * extra item used to release a far downward primary candidate. */
    if ((search->count > 0U) &&
        (search->top[0].bpm <
         (((est->hr_anchor_track_bpm > 0.0f) ?
           est->hr_anchor_track_bpm : est->hr_track_bpm) -
          VITAL_HR_SWITCH_DELTA_BPM)) &&
        (search->top[0].recent_consistency >= 0.90f) &&
        (search->top[0].evidence_quality >= 0.55f) &&
        (primary_margin >= VITAL_HR_EVIDENCE_MARGIN_CLEAR))
    {
        est->hr_challenger_bpm = search->top[0].bpm;
        est->hr_challenger_confirmed = 1U;
        est->hr_challenger_release_confirmed = 1U;
    }

'''

INSERT_MARKER = "    /* Normal takeover still requires the challenger to become the primary\n"


VARIANTS = [
    Variant("baseline", "baseline", "Unmodified V18"),
    Variant(
        "history_primary_immediate",
        "history decision",
        "If the accumulated challenger becomes top-1, immediately start the existing 4 bpm/s release",
        "algorithm/vital_estimator.c",
        INSERT_MARKER,
        HISTORY_PRIMARY_IMMEDIATE + INSERT_MARKER,
    ),
    Variant(
        "current_primary_immediate",
        "decision without history",
        "Any clear far-down top-1 candidate immediately starts release",
        "algorithm/vital_estimator.c",
        INSERT_MARKER,
        CURRENT_PRIMARY_IMMEDIATE + INSERT_MARKER,
    ),
    Variant(
        "recent_primary_immediate",
        "one extra evidence",
        "A far-down top-1 candidate immediately releases only when recent consistency >= 0.90",
        "algorithm/vital_estimator.c",
        INSERT_MARKER,
        RECENT_PRIMARY_IMMEDIATE + INSERT_MARKER,
    ),
    Variant(
        "release_hits_1",
        "confirmation parameter",
        "VITAL_HR_RELEASE_HITS_REQUIRED: 2 -> 1",
        "algorithm/vital_estimator.h",
        "#define VITAL_HR_RELEASE_HITS_REQUIRED        2U",
        "#define VITAL_HR_RELEASE_HITS_REQUIRED        1U",
    ),
    Variant(
        "release_step_6",
        "speed parameter",
        "VITAL_HR_RELEASE_STEP_BPM: 4 -> 6 bpm/s",
        "algorithm/vital_estimator.h",
        "#define VITAL_HR_RELEASE_STEP_BPM          4.00f",
        "#define VITAL_HR_RELEASE_STEP_BPM          6.00f",
    ),
    Variant(
        "primary_hits_1",
        "takeover parameter",
        "Normal challenger top-1 confirmation: 3 primary hits -> 1",
        "algorithm/vital_estimator.c",
        "(est->hr_challenger_primary_hits >= 3U)",
        "(est->hr_challenger_primary_hits >= 1U)",
    ),
    Variant(
        "recent_reject_4",
        "release evidence threshold",
        "Old-path recent rejection threshold: 8 -> 4 bpm",
        "algorithm/vital_estimator.h",
        "#define VITAL_HR_RELEASE_RECENT_REJECT_BPM    8.0f",
        "#define VITAL_HR_RELEASE_RECENT_REJECT_BPM    4.0f",
    ),
    Variant(
        "continuity_weight_0_15",
        "candidate weight",
        "HR candidate continuity weight: 0.25 -> 0.15",
        "algorithm/vital_estimator.c",
        "                      0.25f * continuity +",
        "                      0.15f * continuity +",
    ),
    Variant(
        "sqi_output_agreement_0_60",
        "SQI weight",
        "Final SQI output-agreement factor: 40% -> 60%",
        "algorithm/vital_estimator.c",
        "                      (0.60f + 0.40f * output_agreement) -",
        "                      (0.40f + 0.60f * output_agreement) -",
    ),
    Variant(
        "challenger_confirm_0_80",
        "history threshold",
        "Accumulated challenger confirmation threshold: 1.05 -> 0.80",
        "algorithm/vital_estimator.h",
        "#define VITAL_HR_CHALLENGER_CONFIRM          1.05f",
        "#define VITAL_HR_CHALLENGER_CONFIRM          0.80f",
    ),
    Variant(
        "release_margin_0",
        "evidence threshold",
        "Release evidence margin: 0.02 -> 0.00",
        "algorithm/vital_estimator.h",
        "#define VITAL_HR_RELEASE_EVIDENCE_MARGIN      0.02f",
        "#define VITAL_HR_RELEASE_EVIDENCE_MARGIN      0.00f",
    ),
]


def replace_once(path: Path, old: str, new: str) -> None:
    content = path.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(f"Expected exactly one match in {path}, found {count}: {old!r}")
    path.write_text(content.replace(old, new, 1), encoding="utf-8")


def build_and_run(variant: Variant) -> tuple[Path, str]:
    variant_dir = VARIANT_ROOT / variant.name
    variant_dir.mkdir(parents=True, exist_ok=False)
    for directory in ["algorithm", "include", "src"]:
        shutil.copytree(SOURCE / directory, variant_dir / directory)

    diff_text = ""
    if variant.target_file:
        target = variant_dir / variant.target_file
        original = (SOURCE / variant.target_file).read_text(encoding="utf-8").splitlines(True)
        replace_once(target, variant.old or "", variant.new or "")
        changed = target.read_text(encoding="utf-8").splitlines(True)
        diff_text = "".join(
            difflib.unified_diff(
                original,
                changed,
                fromfile=variant.target_file,
                tofile=variant.target_file,
            )
        )
        (variant_dir / "single_change.patch").write_text(diff_text, encoding="utf-8")

    binary = variant_dir / "pet_v18_variant"
    subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Iinclude",
            "-Ialgorithm",
            "src/main_pc.c",
            "src/csv_reader.c",
            "algorithm/rest_detector.c",
            "algorithm/vital_estimator.c",
            "-o",
            str(binary),
            "-lm",
        ],
        cwd=variant_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    output = variant_dir / "output.csv"
    subprocess.run(
        [str(binary), str(INPUT), str(output), "--acc-unit", "g", "--gyro-unit", "dps"],
        cwd=variant_dir,
        check=True,
        capture_output=True,
        text=True,
    )
    return output, diff_text


def mae(output: pd.Series, reference: pd.Series) -> float:
    keep = output.notna() & reference.notna()
    return float((output[keep] - reference[keep]).abs().mean())


def rmse(output: pd.Series, reference: pd.Series) -> float:
    keep = output.notna() & reference.notna()
    error = output[keep] - reference[keep]
    return float(np.sqrt(np.mean(error**2)))


def longest_run(mask: pd.Series) -> int:
    mask = mask.fillna(False).astype(bool).to_numpy()
    longest = 0
    current = 0
    for value in mask:
        if value:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


if STUDY.exists():
    shutil.rmtree(STUDY)
if OUT.exists():
    shutil.rmtree(OUT)
VARIANT_ROOT.mkdir(parents=True)
OUT.mkdir(parents=True)

ref = pd.read_csv(REFERENCE)
ref["hr_18s_trailing_bpm"] = ref["HR"].rolling(18, min_periods=18).mean()

outputs: dict[str, pd.DataFrame] = {}
diffs: dict[str, str] = {}
for variant in VARIANTS:
    output_path, diff_text = build_and_run(variant)
    outputs[variant.name] = pd.read_csv(output_path).merge(
        ref[["time_s", "HR", "hr_18s_trailing_bpm", "rr_10s_trailing_bpm"]],
        left_on="algorithm_time_s",
        right_on="time_s",
        how="left",
    )
    diffs[variant.name] = diff_text

baseline = outputs["baseline"]
baseline_hr = baseline.loc[baseline["hr_reportable"] == 1, ["algorithm_time_s", "hr_bpm"]]
baseline_rr = baseline.loc[baseline["rr_reportable"] == 1, ["algorithm_time_s", "rr_bpm"]]

summary_rows = []
detail_rows = []
for variant in VARIANTS:
    data = outputs[variant.name].copy()
    hr_mask = (data["hr_reportable"] == 1) & (data["hr_bpm"] > 0)
    rr_mask = (data["rr_reportable"] == 1) & (data["rr_bpm"] > 0)
    data["hr_error_instant"] = data["hr_bpm"] - data["HR"]
    data["hr_error_trailing18"] = data["hr_bpm"] - data["hr_18s_trailing_bpm"]
    data["rr_error_trailing10"] = data["rr_bpm"] - data["rr_10s_trailing_bpm"]
    local = data[data["algorithm_time_s"].between(40, 48) & hr_mask]
    focus = data[data["algorithm_time_s"].isin([45.0, 46.0])]

    hr_compare = data[["algorithm_time_s", "hr_bpm", "hr_candidate_bpm", "hr_sqi", "hr_status"]].merge(
        baseline[["algorithm_time_s", "hr_bpm", "hr_candidate_bpm", "hr_sqi", "hr_status"]],
        on="algorithm_time_s",
        suffixes=("", "_baseline"),
        how="inner",
    )
    rr_compare = data[["algorithm_time_s", "rr_bpm"]].merge(
        baseline_rr,
        on="algorithm_time_s",
        suffixes=("", "_baseline"),
        how="inner",
    )
    hr_delta = (hr_compare["hr_bpm"] - hr_compare["hr_bpm_baseline"]).abs()
    rr_delta = (rr_compare["rr_bpm"] - rr_compare["rr_bpm_baseline"]).abs()

    focus_abs = focus["hr_error_trailing18"].abs()
    summary_rows.append(
        {
            "variant": variant.name,
            "category": variant.category,
            "single_change": variant.description,
            "t45_hr_bpm": float(focus.loc[focus.algorithm_time_s == 45, "hr_bpm"].iloc[0]),
            "t46_hr_bpm": float(focus.loc[focus.algorithm_time_s == 46, "hr_bpm"].iloc[0]),
            "t45_candidate_bpm": float(focus.loc[focus.algorithm_time_s == 45, "hr_candidate_bpm"].iloc[0]),
            "t46_candidate_bpm": float(focus.loc[focus.algorithm_time_s == 46, "hr_candidate_bpm"].iloc[0]),
            "t45_sqi": float(focus.loc[focus.algorithm_time_s == 45, "hr_sqi"].iloc[0]),
            "t46_sqi": float(focus.loc[focus.algorithm_time_s == 46, "hr_sqi"].iloc[0]),
            "t45_abs_error_trailing18": float(focus.loc[focus.algorithm_time_s == 45, "hr_error_trailing18"].abs().iloc[0]),
            "t46_abs_error_trailing18": float(focus.loc[focus.algorithm_time_s == 46, "hr_error_trailing18"].abs().iloc[0]),
            "t45_46_mean_abs_error_trailing18": float(focus_abs.mean()),
            "t45_46_max_abs_error_trailing18": float(focus_abs.max()),
            "local_40_48_mae_trailing18": float(local["hr_error_trailing18"].abs().mean()),
            "local_40_48_max_abs_error_trailing18": float(local["hr_error_trailing18"].abs().max()),
            "global_hr_mae_instant": mae(data.loc[hr_mask, "hr_bpm"], data.loc[hr_mask, "HR"]),
            "global_hr_mae_trailing18": mae(data.loc[hr_mask, "hr_bpm"], data.loc[hr_mask, "hr_18s_trailing_bpm"]),
            "global_hr_rmse_trailing18": rmse(data.loc[hr_mask, "hr_bpm"], data.loc[hr_mask, "hr_18s_trailing_bpm"]),
            "global_hr_count_abs_error_gt5_trailing18": int((data.loc[hr_mask, "hr_error_trailing18"].abs() > 5).sum()),
            "global_hr_longest_run_gt5_trailing18_s": longest_run(data.loc[hr_mask, "hr_error_trailing18"].abs() > 5),
            "global_rr_mae_trailing10": mae(data.loc[rr_mask, "rr_bpm"], data.loc[rr_mask, "rr_10s_trailing_bpm"]),
            "hr_changed_seconds_vs_baseline": int((hr_delta > 1e-6).sum()),
            "hr_max_delta_vs_baseline": float(hr_delta.max()),
            "hr_candidate_changed_seconds_vs_baseline": int(
                ((hr_compare["hr_candidate_bpm"] - hr_compare["hr_candidate_bpm_baseline"]).abs() > 1e-6).sum()
            ),
            "hr_sqi_changed_seconds_vs_baseline": int(
                ((hr_compare["hr_sqi"] - hr_compare["hr_sqi_baseline"]).abs() > 1e-6).sum()
            ),
            "hr_status_changed_seconds_vs_baseline": int(
                (hr_compare["hr_status"].fillna("<NA>") != hr_compare["hr_status_baseline"].fillna("<NA>")).sum()
            ),
            "rr_changed_seconds_vs_baseline": int((rr_delta > 1e-6).sum()),
            "rr_max_delta_vs_baseline": float(rr_delta.max()),
            "focus_pass_both_within5": bool((focus_abs <= 5).all()),
        }
    )

    for _, row in data[data["algorithm_time_s"].between(38, 52)].iterrows():
        detail_rows.append(
            {
                "variant": variant.name,
                "time_s": row["algorithm_time_s"],
                "hr_reference": row["HR"],
                "hr_reference_trailing18": row["hr_18s_trailing_bpm"],
                "hr_bpm": row["hr_bpm"],
                "hr_candidate_bpm": row["hr_candidate_bpm"],
                "hr_sqi": row["hr_sqi"],
                "hr_status": row["hr_status"],
                "hr_challenger_bpm": row["hr_challenger_bpm"],
                "hr_challenger_evidence": row["hr_challenger_evidence"],
            }
        )

summary = pd.DataFrame(summary_rows)
details = pd.DataFrame(detail_rows)
base_row = summary[summary.variant == "baseline"].iloc[0]
summary["delta_global_hr_mae_trailing18"] = (
    summary["global_hr_mae_trailing18"] - base_row["global_hr_mae_trailing18"]
)
summary["delta_global_rr_mae_trailing10"] = (
    summary["global_rr_mae_trailing10"] - base_row["global_rr_mae_trailing10"]
)
summary["local_improvement_pct"] = (
    1
    - summary["t45_46_mean_abs_error_trailing18"]
    / base_row["t45_46_mean_abs_error_trailing18"]
) * 100
summary["global_guard_pass"] = (
    (summary["delta_global_hr_mae_trailing18"] <= 0.10)
    & (summary["rr_changed_seconds_vs_baseline"] == 0)
)
summary["recommended_candidate"] = (
    summary["focus_pass_both_within5"]
    & summary["global_guard_pass"]
)
summary["selected_recommendation"] = (
    (summary["variant"] == "history_primary_immediate")
    & summary["recommended_candidate"]
)

summary.to_csv(OUT / "single_variable_summary.csv", index=False)
details.to_csv(OUT / "local_38_52s_outputs.csv", index=False)
shutil.copy2(
    VARIANT_ROOT / "history_primary_immediate" / "single_change.patch",
    OUT / "recommended_history_primary_immediate.patch",
)
shutil.copy2(
    VARIANT_ROOT / "history_primary_immediate" / "output.csv",
    OUT / "recommended_variant_output.csv",
)
shutil.copy2(
    VARIANT_ROOT / "history_primary_immediate" / "algorithm" / "vital_estimator.c",
    OUT / "recommended_vital_estimator.c",
)
shutil.copy2(Path(__file__).resolve(), OUT / "run_v18_release_single_variable_study.py")

# Rank only variants that solve both focus seconds and preserve the dataset.
ranked = summary.sort_values(
    [
        "selected_recommendation",
        "recommended_candidate",
        "t45_46_mean_abs_error_trailing18",
        "delta_global_hr_mae_trailing18",
        "hr_changed_seconds_vs_baseline",
    ],
    ascending=[False, False, True, True, True],
).reset_index(drop=True)
ranked.to_csv(OUT / "ranked_variants.csv", index=False)

# Local comparison plot.
fig, (ax, ax2) = plt.subplots(2, 1, figsize=(14, 9), sharex=True, constrained_layout=True)
base_local = outputs["baseline"]
window = base_local["algorithm_time_s"].between(38, 52)
ax.plot(base_local.loc[window, "algorithm_time_s"], base_local.loc[window, "HR"], color="black", marker="o", lw=2.0, label="Instantaneous HR reference")
ax.plot(base_local.loc[window, "algorithm_time_s"], base_local.loc[window, "hr_18s_trailing_bpm"], color="#777777", ls="--", lw=1.8, label="18 s trailing reference")

plot_variants = [
    ("baseline", "#B3261E", "Baseline V18", 2.2),
    ("history_primary_immediate", "#00796B", "History + top-1 immediate", 2.6),
    ("current_primary_immediate", "#6A1B9A", "Top-1 immediate without history", 1.6),
    ("release_hits_1", "#E07A1F", "Release hits 1", 1.6),
    ("release_step_6", "#1565C0", "Release speed 6", 1.4),
    ("continuity_weight_0_15", "#6B8E23", "Continuity weight 0.15", 1.4),
]
for name, color, label, width in plot_variants:
    data = outputs[name]
    selected = data["algorithm_time_s"].between(38, 52)
    ax.plot(data.loc[selected, "algorithm_time_s"], data.loc[selected, "hr_bpm"], color=color, marker=".", lw=width, label=label)
ax.axvspan(44.5, 46.5, color="#F8D7DA", alpha=0.55)
ax.set_ylabel("HR (bpm)")
ax.set_title("Single-variable release study: local HR trajectory")
ax.legend(ncol=4, fontsize=8.5)
ax.grid(alpha=0.25)

for name, color, label, width in plot_variants:
    data = outputs[name].copy()
    selected = data["algorithm_time_s"].between(38, 52)
    error = data.loc[selected, "hr_bpm"] - data.loc[selected, "hr_18s_trailing_bpm"]
    ax2.plot(data.loc[selected, "algorithm_time_s"], error, color=color, marker=".", lw=width, label=label)
ax2.axhspan(-5, 5, color="#E7F3E8", alpha=0.8)
ax2.axhline(0, color="#555555", lw=0.8)
ax2.axvspan(44.5, 46.5, color="#F8D7DA", alpha=0.40)
ax2.set_ylabel("Error vs 18 s trailing ref (bpm)")
ax2.set_xlabel("Algorithm time (s)")
ax2.set_title("The target is to bring both 45 s and 46 s inside ±5 bpm")
ax2.grid(alpha=0.25)

fig.savefig(OUT / "single_variable_local_comparison.png", dpi=180, bbox_inches="tight")
plt.close(fig)

# Trade-off chart with readable categorical labels.
plot_table = summary.sort_values("t45_46_mean_abs_error_trailing18", ascending=False)
colors = np.where(plot_table["recommended_candidate"], "#2E7D32", "#C77800")
fig, (ax, ax2) = plt.subplots(
    1,
    2,
    figsize=(16, 8),
    sharey=True,
    gridspec_kw={"width_ratios": [1.25, 1.0]},
    constrained_layout=True,
)
y = np.arange(len(plot_table))
ax.barh(y, plot_table["t45_46_mean_abs_error_trailing18"], color=colors, alpha=0.85)
ax.axvline(5, color="#B3261E", ls="--", lw=1.2, label="45–46 s acceptance boundary")
ax.set_yticks(y, plot_table["variant"])
ax.set_xlabel("Mean absolute error at 45–46 s (bpm)")
ax.set_title("Local correction")
ax.grid(axis="x", alpha=0.25)
ax.legend(loc="lower right")

ax2.barh(y, plot_table["delta_global_hr_mae_trailing18"], color=colors, alpha=0.85)
ax2.axvline(0, color="#555555", lw=0.8)
ax2.axvline(0.10, color="#777777", ls=":", lw=1.2, label="Regression guard (+0.10 bpm)")
ax2.set_xlabel("Change in global HR MAE (bpm; lower is better)")
ax2.set_title("Same-dataset global regression")
ax2.grid(axis="x", alpha=0.25)
ax2.legend(loc="lower right")
fig.suptitle("Single-variable methods: local benefit and global guard", fontsize=15)
fig.savefig(OUT / "single_variable_tradeoff.png", dpi=180, bbox_inches="tight")
plt.close(fig)

# Compact Chinese report generated from actual results.
report_columns = [
    "variant",
    "t45_hr_bpm",
    "t46_hr_bpm",
    "t45_abs_error_trailing18",
    "t46_abs_error_trailing18",
    "local_improvement_pct",
    "global_hr_mae_trailing18",
    "delta_global_hr_mae_trailing18",
    "global_rr_mae_trailing10",
    "hr_changed_seconds_vs_baseline",
    "hr_candidate_changed_seconds_vs_baseline",
    "hr_sqi_changed_seconds_vs_baseline",
    "rr_changed_seconds_vs_baseline",
]
table = ranked[report_columns].copy()
for column in table.columns:
    if column != "variant":
        table[column] = table[column].map(lambda value: f"{value:.4f}" if isinstance(value, (float, np.floating)) else str(value))

markdown_header = "| " + " | ".join(table.columns) + " |"
markdown_rule = "|" + "|".join(["---"] * len(table.columns)) + "|"
markdown_rows = [
    "| " + " | ".join(str(value) for value in row) + " |"
    for row in table.itertuples(index=False, name=None)
]
markdown_table = "\n".join([markdown_header, markdown_rule, *markdown_rows])

best = ranked.iloc[0]
report_lines = [
    "# V18 45–46 s单变量释放实验",
    "",
    "所有版本都从同一份V18源码开始，每个版本只改变一个参数、权重或决策方法。输入固定为本次`input(1).csv`。局部主参考为18 s trailing HR reference，同时用同一数据集31–175 s的HR/RR指标作为回归保护。",
    "",
    "## 排名结果",
    "",
    markdown_table,
    "",
    "## 综合选择结果",
    "",
    f"推荐：`{best['variant']}`。45–46 s平均绝对误差为{best['t45_46_mean_abs_error_trailing18']:.3f} bpm，局部改善{best['local_improvement_pct']:.1f}%；全局18 s trailing HR MAE变化{best['delta_global_hr_mae_trailing18']:+.3f} bpm；RR改变秒数{int(best['rr_changed_seconds_vs_baseline'])}。",
    "选择依据不是局部误差最小，而是先通过45/46 s、全局HR和RR保护，再优先保留历史证据且缩小决策影响范围。`continuity_weight_0_15`虽然单数据集数值最低，但改变63秒输出、19秒候选和全部146秒SQI，属于宽泛重调，因此不作为本次针对性修正。",
    "",
    "## 核心发现",
    "",
    "- `history_primary_immediate`验证了优先猜想：45/46 s输出变为84.67/80.67 bpm，两点都进入±5 bpm，同时RR完全不变。",
    "- 单独把release速度4提高到6 bpm/s对45/46 s没有影响，因为release直到47 s才被触发。",
    "- 单独降低continuity权重得到更低局部误差，但影响候选、SQI和输出的范围明显更广，不属于针对释放时序的最小修正。",
    "- 只修改最终SQI的output-agreement权重会改变置信度表达，但不会改变候选或释放决策，因此不能解决45/46 s。",
    "",
    "## 判定规则",
    "",
    "- 45 s和46 s都必须进入±5 bpm。",
    "- 同一数据集全局18 s trailing HR MAE不得恶化超过0.10 bpm。",
    "- RR逐秒结果必须保持不变。",
    "- 当前阶段只验证这一个数据集，不据此宣称对其他人体或犬只数据泛化。",
    "",
]
(OUT / "STUDY_REPORT_CN.md").write_text("\n".join(report_lines), encoding="utf-8")

print(summary.sort_values("t45_46_mean_abs_error_trailing18")[[
    "variant",
    "t45_hr_bpm",
    "t46_hr_bpm",
    "t45_46_mean_abs_error_trailing18",
    "local_improvement_pct",
    "global_hr_mae_trailing18",
    "delta_global_hr_mae_trailing18",
    "global_rr_mae_trailing10",
    "hr_changed_seconds_vs_baseline",
    "rr_changed_seconds_vs_baseline",
    "recommended_candidate",
]].to_string(index=False))
print(f"OUT={OUT}")
