#!/usr/bin/env python3
"""Comprehensive V15/V17/V18 regression evaluation for the recordings."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parent
PLOTS = ROOT / "plots_v18"
PLOTS.mkdir(exist_ok=True)

DATASETS = {
    "stable": {"hr_col": "HR", "duration": 235},
    "rr_change": {"hr_col": "HR", "duration": 235},
    "disruption": {"hr_col": "Unnamed: 11", "duration": 236},
}

MOTION_SEGMENTS = {
    "disruption": [
        ("stable_before_sway", 40, 68),
        ("low_amplitude_sway", 69, 100),
        ("after_sway", 101, 140),
        ("short_adjustment", 141, 146),
        ("before_moderate", 147, 155),
        ("moderate_motion", 156, 159),
        ("recovery", 160, 201),
        ("severe_motion", 202, 204),
        ("post_reset", 205, 236),
    ]
}


def safe_corr(first, second):
    first = np.asarray(first, dtype=float)
    second = np.asarray(second, dtype=float)
    if len(first) < 3 or np.std(first) <= 0 or np.std(second) <= 0:
        return np.nan
    return float(np.corrcoef(first, second)[0, 1])


def load_aligned(dataset, filename):
    folder = ROOT / "datasets" / dataset
    output = pd.read_csv(folder / filename)
    reference = pd.read_csv(folder / "reference.csv").set_index("time_s")
    mapped_time = (output["algorithm_time_s"] - 1).round().astype(int)
    hr = reference[DATASETS[dataset]["hr_col"]]
    output["hr_reference"] = mapped_time.map(hr)
    output["hr_reference_trailing18"] = mapped_time.map(hr.rolling(18, min_periods=1).mean())
    output["rr_reference_10s"] = mapped_time.map(reference["rr_10s_trailing_bpm"])
    return output, reference


def mode_mask(frame, mode):
    if mode == "HR_ACCEPTED":
        return frame["hr_status"].isin(["VALID", "VALID_LOW"])
    if mode == "HR_REPORTABLE":
        return frame["hr_reportable"].eq(1)
    if mode == "HR_ALL_TRACKED":
        return frame["vital_estimate_returned"].eq(1)
    if mode == "RR_VALID":
        return frame["rr_valid"].eq(1)
    if mode == "RR_REPORTABLE":
        return frame["rr_reportable"].eq(1)
    raise ValueError(mode)


def longest_bad_run(frame, mask, error_threshold=8.0):
    rows = frame.loc[mask, ["algorithm_time_s", "hr_bpm", "hr_reference"]].copy()
    rows["bad"] = (rows["hr_bpm"] - rows["hr_reference"]).abs().gt(error_threshold)
    longest = 0
    current = 0
    runs_at_least_three = 0
    previous_time = None
    for row in rows.itertuples(index=False):
        contiguous = previous_time is not None and abs(row.algorithm_time_s - previous_time - 1.0) < 1e-6
        if row.bad and contiguous:
            current += 1
        elif row.bad:
            if current >= 3:
                runs_at_least_three += 1
            current = 1
        else:
            if current >= 3:
                runs_at_least_three += 1
            current = 0
        longest = max(longest, current)
        previous_time = row.algorithm_time_s
    if current >= 3:
        runs_at_least_three += 1
    return longest, runs_at_least_three


def evaluate_hr(frame, mode, reference_column):
    eligible = (
        frame["vital_estimate_returned"].eq(1)
        & frame[reference_column].notna()
    )
    mask = mode_mask(frame, mode) & eligible & frame["hr_bpm"].gt(0)
    error = frame.loc[mask, "hr_bpm"] - frame.loc[mask, reference_column]
    prediction = frame.loc[mask, "hr_bpm"]
    reference = frame.loc[mask, reference_column]
    adjacent = prediction.diff().abs().dropna()
    longest, bad_runs = longest_bad_run(frame, mask) if reference_column == "hr_reference" else (np.nan, np.nan)

    direction_frame = frame.loc[mask, ["algorithm_time_s", "hr_bpm", reference_column]].copy()
    prediction_change = direction_frame["hr_bpm"].diff(5)
    reference_change = direction_frame[reference_column].diff(5)
    direction_mask = reference_change.abs().ge(1.0) & prediction_change.notna()
    direction_agreement = (
        np.sign(prediction_change[direction_mask]) == np.sign(reference_change[direction_mask])
    ).mean() if direction_mask.any() else np.nan

    return {
        "mode": mode,
        "reference": reference_column,
        "count": int(mask.sum()),
        "eligible_count": int(eligible.sum()),
        "coverage": float(mask.sum() / eligible.sum()) if eligible.sum() else np.nan,
        "mae": float(error.abs().mean()),
        "bias": float(error.mean()),
        "rmse": float(np.sqrt(np.mean(error**2))),
        "correlation": safe_corr(prediction, reference),
        "direction_agreement_5s": float(direction_agreement),
        "mean_adjacent_step": float(adjacent.mean()) if len(adjacent) else np.nan,
        "p95_adjacent_step": float(adjacent.quantile(0.95)) if len(adjacent) else np.nan,
        "max_adjacent_step": float(adjacent.max()) if len(adjacent) else np.nan,
        "abs_error_gt8_count": int(error.abs().gt(8).sum()),
        "longest_error_gt8_run": longest,
        "error_gt8_runs_at_least_3s": bad_runs,
    }


def best_lag_diagnostic(frame, mode, max_lag=12):
    best = None
    for lag in range(max_lag + 1):
        shifted_reference = frame["hr_reference"].shift(lag)
        mask = mode_mask(frame, mode) & frame["hr_bpm"].gt(0) & shifted_reference.notna()
        correlation = safe_corr(frame.loc[mask, "hr_bpm"], shifted_reference[mask])
        error = frame.loc[mask, "hr_bpm"] - shifted_reference[mask]
        candidate = {
            "lag_s": lag,
            "count": int(mask.sum()),
            "mae": float(error.abs().mean()),
            "bias": float(error.mean()),
            "correlation": correlation,
            "amplitude_ratio": float(
                frame.loc[mask, "hr_bpm"].std() / (shifted_reference[mask].std() + 1e-12)
            ),
        }
        if best is None or candidate["correlation"] > best["correlation"]:
            best = candidate
    return best


frames = {}
metric_rows = []
lag_rows = []
for dataset in DATASETS:
    frames[dataset] = {}
    for algorithm, filename in [
        ("V15", "v15_results.csv"),
        ("V17", "v17_results.csv"),
        ("V18", "v18_results.csv"),
    ]:
        frame, reference = load_aligned(dataset, filename)
        frames[dataset][algorithm] = frame
        for mode in ["HR_ACCEPTED", "HR_REPORTABLE", "HR_ALL_TRACKED"]:
            for reference_column in ["hr_reference", "hr_reference_trailing18"]:
                metric_rows.append({
                    "dataset": dataset,
                    "algorithm": algorithm,
                    **evaluate_hr(frame, mode, reference_column),
                })
        for mode in ["RR_VALID", "RR_REPORTABLE"]:
            mask = mode_mask(frame, mode) & frame["rr_bpm"].gt(0) & frame["rr_reference_10s"].notna()
            error = frame.loc[mask, "rr_bpm"] - frame.loc[mask, "rr_reference_10s"]
            metric_rows.append({
                "dataset": dataset,
                "algorithm": algorithm,
                "mode": mode,
                "reference": "rr_reference_10s",
                "count": int(mask.sum()),
                "eligible_count": int((frame["vital_estimate_returned"].eq(1) & frame["rr_reference_10s"].notna()).sum()),
                "coverage": float(mask.mean()),
                "mae": float(error.abs().mean()),
                "bias": float(error.mean()),
                "rmse": float(np.sqrt(np.mean(error**2))),
                "correlation": safe_corr(frame.loc[mask, "rr_bpm"], frame.loc[mask, "rr_reference_10s"]),
            })
        lag_rows.append({
            "dataset": dataset,
            "algorithm": algorithm,
            "mode": "HR_REPORTABLE",
            **best_lag_diagnostic(frame, "HR_REPORTABLE"),
        })

metrics = pd.DataFrame(metric_rows)
metrics.to_csv(ROOT / "metrics_summary_v18.csv", index=False)
pd.DataFrame(lag_rows).to_csv(ROOT / "hr_lag_diagnostic_v18.csv", index=False)

# SQI calibration: high SQI should progressively reduce large errors.
sqi_rows = []
for dataset in DATASETS:
    for algorithm in ["V15", "V17", "V18"]:
        frame = frames[dataset][algorithm]
        base = frame["vital_estimate_returned"].eq(1) & frame["hr_bpm"].gt(0) & frame["hr_reference"].notna()
        error = (frame["hr_bpm"] - frame["hr_reference"]).abs()
        for threshold in [45, 50, 55, 65, 70, 75, 80]:
            mask = base & frame["hr_sqi"].ge(threshold)
            sqi_rows.append({
                "dataset": dataset,
                "algorithm": algorithm,
                "sqi_threshold": threshold,
                "count": int(mask.sum()),
                "coverage_of_estimable": float(mask.sum() / base.sum()) if base.sum() else np.nan,
                "mae": float(error[mask].mean()),
                "error_gt8_count": int(error[mask].gt(8).sum()),
                "error_gt8_fraction": float(error[mask].gt(8).mean()) if mask.sum() else np.nan,
            })
pd.DataFrame(sqi_rows).to_csv(ROOT / "hr_sqi_calibration_v18.csv", index=False)

# Verify that HR-only V18 work did not alter RR or the rest/motion state machine.
invariant_columns = [
    "rest_state", "local_label", "clean_ratio_10s", "clean_ratio_20s",
    "strong_motion", "rr_bpm", "rr_valid", "rr_reportable", "rr_sqi",
    "rr_snr", "rr_axis", "rr_candidate_bpm",
]
invariant_rows = []
for dataset in DATASETS:
    first = frames[dataset]["V17"]
    second = frames[dataset]["V18"]
    for column in invariant_columns:
        if pd.api.types.is_numeric_dtype(first[column]):
            difference = (first[column].fillna(0) - second[column].fillna(0)).abs()
            mismatch = difference.gt(1e-6)
            maximum = float(difference.max())
        else:
            mismatch = first[column].fillna("").ne(second[column].fillna(""))
            maximum = np.nan
        invariant_rows.append({
            "dataset": dataset,
            "column": column,
            "mismatch_count": int(mismatch.sum()),
            "maximum_absolute_difference": maximum,
        })
pd.DataFrame(invariant_rows).to_csv(ROOT / "rr_rest_invariance_v18.csv", index=False)

# Disruption interval regression.
motion_rows = []
for algorithm in ["V15", "V17", "V18"]:
    frame = frames["disruption"][algorithm]
    for segment, start, end in MOTION_SEGMENTS["disruption"]:
        part = frame[frame["algorithm_time_s"].between(start, end)]
        hr_mask = part["hr_reportable"].eq(1) & part["hr_reference"].notna() & part["hr_bpm"].gt(0)
        error = part.loc[hr_mask, "hr_bpm"] - part.loc[hr_mask, "hr_reference"]
        motion_rows.append({
            "algorithm": algorithm,
            "segment": segment,
            "start_s": start,
            "end_s": end,
            "rest_measurement_fraction": float(part["rest_state"].eq("REST_MEASUREMENT").mean()),
            "moderate_rows": int(part["motion_class"].eq("MODERATE").sum()),
            "left_measurement_rows": int(part["left_measurement"].fillna(0).sum()),
            "vital_reset_rows": int(part["vital_reset_since_previous_second"].fillna(0).sum()),
            "hr_reportable_count": int(hr_mask.sum()),
            "hr_mae": float(error.abs().mean()) if len(error) else np.nan,
            "hr_bias": float(error.mean()) if len(error) else np.nan,
        })
pd.DataFrame(motion_rows).to_csv(ROOT / "motion_segment_summary_v18.csv", index=False)

# Explicitly track the severe stable-recording failure that motivated V18.
local_rows = []
for algorithm in ["V15", "V17", "V18"]:
    frame = frames["stable"][algorithm]
    for segment, start, end in [
        ("pre_transition", 111, 120),
        ("transition_entry", 121, 130),
        ("severe_old_path", 131, 140),
        ("combined_failure", 121, 140),
        ("recovery", 141, 155),
    ]:
        part = frame[frame["algorithm_time_s"].between(start, end)]
        mask = part["hr_reportable"].eq(1) & part["hr_reference"].notna() & part["hr_bpm"].gt(0)
        error = part.loc[mask, "hr_bpm"] - part.loc[mask, "hr_reference"]
        local_rows.append({
            "algorithm": algorithm,
            "segment": segment,
            "start_s": start,
            "end_s": end,
            "count": int(mask.sum()),
            "mae": float(error.abs().mean()),
            "bias": float(error.mean()),
            "max_abs_error": float(error.abs().max()),
            "error_gt8_count": int(error.abs().gt(8).sum()),
        })
pd.DataFrame(local_rows).to_csv(ROOT / "hr_local_failure_summary_v18.csv", index=False)

# V18 feature-use summary.
feature_rows = []
for dataset in DATASETS:
    frame = frames[dataset]["V18"]
    estimable = frame["vital_estimate_returned"].eq(1)
    feature_rows.append({
        "dataset": dataset,
        "estimable_rows": int(estimable.sum()),
        "consensus_rows": int((estimable & frame["hr_consensus_used"].eq(1)).sum()),
        "consensus_fraction": float(frame.loc[estimable, "hr_consensus_used"].mean()),
        "trend_rows": int((estimable & frame["hr_trend_active"].eq(1)).sum()),
        "axis_switches": int(
            frame.loc[estimable, "hr_axis"].ne(frame.loc[estimable, "hr_axis"].shift()).sum() - 1
        ),
        "mean_rr_suspicion": float(frame.loc[estimable, "hr_rr_suspicion"].mean()),
        "p95_rr_suspicion": float(frame.loc[estimable, "hr_rr_suspicion"].quantile(0.95)),
    })
pd.DataFrame(feature_rows).to_csv(ROOT / "hr_evidence_usage_v18.csv", index=False)

# Comparison plots.
for dataset in DATASETS:
    v15 = frames[dataset]["V15"]
    v17 = frames[dataset]["V17"]
    v18 = frames[dataset]["V18"]
    figure, axes = plt.subplots(3, 1, figsize=(13, 10), sharex=True,
                                gridspec_kw={"height_ratios": [2.2, 1.2, 1.1]})

    axes[0].plot(v18["algorithm_time_s"], v18["hr_reference"], label="Reference HR", linewidth=2.0)
    axes[0].plot(v18["algorithm_time_s"], v18["hr_reference_trailing18"],
                 label="Reference HR, 18 s trailing mean", linewidth=1.5, alpha=0.8)
    axes[0].plot(v15["algorithm_time_s"], v15["hr_bpm"].where(v15["hr_reportable"].eq(1)),
                 label="V15 reportable", linewidth=1.5)
    axes[0].plot(v17["algorithm_time_s"], v17["hr_bpm"].where(v17["hr_reportable"].eq(1)),
                 label="V17 reportable", linewidth=1.3, alpha=0.75)
    axes[0].plot(v18["algorithm_time_s"], v18["hr_bpm"].where(v18["hr_reportable"].eq(1)),
                 label="V18 reportable", linewidth=1.9)
    axes[0].set_ylabel("HR (bpm)")
    axes[0].set_title(f"{dataset}: V15 versus V17 versus V18 HR")
    axes[0].grid(alpha=0.25)
    axes[0].legend(ncol=2)

    estimable = v18["vital_estimate_returned"].eq(1) & v18["hr_bpm"].gt(0)
    absolute_error = (v18["hr_bpm"] - v18["hr_reference"]).abs().where(estimable)
    axes[1].plot(v18["algorithm_time_s"], absolute_error, label="V18 absolute error", color="tab:red")
    axes[1].axhline(8, color="tab:red", linestyle="--", linewidth=1, alpha=0.6)
    sqi_axis = axes[1].twinx()
    sqi_axis.plot(v18["algorithm_time_s"], v18["hr_sqi"].where(estimable),
                  label="V18 HR SQI", color="tab:green", alpha=0.8)
    sqi_axis.axhline(65, color="tab:green", linestyle=":", linewidth=1)
    axes[1].set_ylabel("Absolute error (bpm)")
    sqi_axis.set_ylabel("SQI")
    axes[1].grid(alpha=0.2)

    axes[2].plot(v18["algorithm_time_s"], v18["hr_cand1_bpm"], label="Candidate 1", alpha=0.65)
    axes[2].plot(v18["algorithm_time_s"], v18["hr_cand2_bpm"], label="Candidate 2", alpha=0.45)
    axes[2].plot(v18["algorithm_time_s"], v18["hr_cand3_bpm"], label="Candidate 3", alpha=0.35)
    consensus = v18["hr_consensus_used"].eq(1)
    axes[2].scatter(v18.loc[consensus, "algorithm_time_s"], v18.loc[consensus, "hr_candidate_bpm"],
                    label="Consensus target", s=16, color="black")
    axes[2].set_xlabel("Algorithm time (s)")
    axes[2].set_ylabel("Candidate (bpm)")
    axes[2].grid(alpha=0.2)
    axes[2].legend(ncol=4, fontsize=9)

    if dataset == "disruption":
        for axis in axes:
            axis.axvspan(69, 100, color="gold", alpha=0.08)
            axis.axvspan(156, 159, color="orange", alpha=0.12)
            axis.axvspan(202, 204, color="red", alpha=0.10)

    figure.tight_layout()
    figure.savefig(PLOTS / f"{dataset}_hr_v18.png", dpi=180)
    plt.close(figure)

# Focused view of the stale-high-path release.
stable_v15 = frames["stable"]["V15"]
stable_v17 = frames["stable"]["V17"]
stable_v18 = frames["stable"]["V18"]
focus = stable_v18["algorithm_time_s"].between(118, 150)
figure, axes = plt.subplots(2, 1, figsize=(12, 7), sharex=True,
                            gridspec_kw={"height_ratios": [2.0, 1.0]})
axes[0].plot(stable_v18.loc[focus, "algorithm_time_s"],
             stable_v18.loc[focus, "hr_reference"], label="Reference", linewidth=2.2)
for frame, label, width in [
    (stable_v15, "V15", 1.2),
    (stable_v17, "V17", 1.4),
    (stable_v18, "V18", 2.1),
]:
    mask = frame["algorithm_time_s"].between(118, 150)
    axes[0].plot(frame.loc[mask, "algorithm_time_s"], frame.loc[mask, "hr_bpm"],
                 label=label, linewidth=width)
    axes[1].plot(frame.loc[mask, "algorithm_time_s"],
                 (frame.loc[mask, "hr_bpm"] - frame.loc[mask, "hr_reference"]).abs(),
                 label=f"{label} absolute error", linewidth=width)
for axis in axes:
    axis.axvspan(131, 140, color="tab:red", alpha=0.08)
    axis.grid(alpha=0.25)
axes[0].set_ylabel("HR (bpm)")
axes[0].set_title("stable: focused stale-high-path release")
axes[0].legend(ncol=4)
axes[1].axhline(8, color="tab:red", linestyle="--", linewidth=1)
axes[1].set_ylabel("Absolute error (bpm)")
axes[1].set_xlabel("Algorithm time (s)")
axes[1].legend(ncol=3)
figure.tight_layout()
figure.savefig(PLOTS / "stable_hr_focus_118_150_v18.png", dpi=180)
plt.close(figure)

print(
    metrics[
        (metrics["mode"].isin(["HR_ACCEPTED", "HR_REPORTABLE"]))
        & (metrics["reference"] == "hr_reference")
    ][["dataset", "algorithm", "mode", "count", "coverage", "mae", "bias", "correlation",
       "direction_agreement_5s", "abs_error_gt8_count", "longest_error_gt8_run"]]
    .round(4)
    .to_string(index=False)
)
