#!/usr/bin/env python3
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent
PLOTS = ROOT / "plots"
PLOTS.mkdir(exist_ok=True)

DATASETS = {
    "stable": {"hr_col": "HR"},
    "rr_change": {"hr_col": "HR"},
    "disruption": {"hr_col": "Unnamed: 11"},
}

ALGORITHMS = {
    "Baseline": {"file": "baseline_results.csv", "cadence": 2.0},
    "V14": {"file": "v14_results.csv", "cadence": 1.0},
}

def safe_corr(a, b):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    if len(a) < 3 or np.std(a) == 0 or np.std(b) == 0:
        return np.nan
    return float(np.corrcoef(a, b)[0, 1])

def align(dataset, algorithm):
    cfg = DATASETS[dataset]
    folder = ROOT / "datasets" / dataset
    out = pd.read_csv(folder / ALGORITHMS[algorithm]["file"])
    ref = pd.read_csv(folder / "reference.csv").set_index("time_s")
    out["reference_time_s"] = (out["algorithm_time_s"] - 1).round().astype(int)
    out["hr_reference"] = out["reference_time_s"].map(ref[cfg["hr_col"]])
    out["rr_reference_10s"] = out["reference_time_s"].map(ref["rr_10s_trailing_bpm"])
    return out

def metric_row(dataset, algorithm, mode, frame):
    cadence = ALGORITHMS[algorithm]["cadence"]
    if mode == "HR_ACCEPTED":
        valid = frame["hr_status"].isin(["VALID", "VALID_LOW"])
        pred, ref, sqi, bad_threshold = "hr_bpm", "hr_reference", "hr_sqi", 8.0
    elif mode == "HR_VALID_ONLY":
        valid = frame["hr_status"].eq("VALID")
        pred, ref, sqi, bad_threshold = "hr_bpm", "hr_reference", "hr_sqi", 8.0
    elif mode == "RR_VALID":
        valid = frame["rr_valid"].eq(1)
        pred, ref, sqi, bad_threshold = "rr_bpm", "rr_reference_10s", "rr_sqi", 5.0
    elif mode == "RR_ALL_TRACKED":
        valid = frame["vital_estimate_returned"].eq(1) & frame["rr_bpm"].gt(0)
        pred, ref, sqi, bad_threshold = "rr_bpm", "rr_reference_10s", "rr_sqi", 5.0
    else:
        raise ValueError(mode)

    valid &= frame[pred].notna() & frame[ref].notna() & frame[pred].gt(0)
    eligible = frame["vital_estimate_returned"].eq(1) & frame[ref].notna()
    sub = frame.loc[valid, ["algorithm_time_s", pred, ref, sqi]].copy()
    err = sub[pred] - sub[ref]
    dt = sub["algorithm_time_s"].diff()
    step = sub[pred].diff().abs()
    adjacent = step[(dt > 0) & (dt <= cadence + 0.1)]
    high = sub[sqi].ge(80)

    return {
        "dataset": dataset,
        "algorithm": algorithm,
        "mode": mode,
        "count": int(valid.sum()),
        "eligible_count": int(eligible.sum()),
        "coverage": float(valid.sum() / eligible.sum()) if eligible.sum() else np.nan,
        "mae": float(err.abs().mean()),
        "bias": float(err.mean()),
        "rmse": float(np.sqrt(np.mean(err ** 2))),
        "correlation": safe_corr(sub[pred], sub[ref]),
        "mean_adjacent_step": float(adjacent.mean()),
        "p95_adjacent_step": float(adjacent.quantile(0.95)),
        "max_adjacent_step": float(adjacent.max()),
        "high_sqi_count": int(high.sum()),
        "high_sqi_bad_count": int((high & err.abs().gt(bad_threshold)).sum()),
    }

aligned = {}
rows = []
for dataset in DATASETS:
    aligned[dataset] = {}
    for algorithm in ALGORITHMS:
        frame = align(dataset, algorithm)
        aligned[dataset][algorithm] = frame
        for mode in ["HR_ACCEPTED", "HR_VALID_ONLY", "RR_VALID", "RR_ALL_TRACKED"]:
            rows.append(metric_row(dataset, algorithm, mode, frame))

metrics = pd.DataFrame(rows)
metrics.to_csv(ROOT / "metrics_summary.csv", index=False)

# RR-change response interval, deliberately includes invalid tracked outputs.
transition_rows = []
for algorithm in ALGORITHMS:
    frame = aligned["rr_change"][algorithm]
    segment = frame[frame["algorithm_time_s"].between(70, 115)].copy()
    for mode in ["RR_VALID", "RR_ALL_TRACKED"]:
        row = metric_row("rr_change_70_115s", algorithm, mode, segment)
        transition_rows.append(row)
pd.DataFrame(transition_rows).to_csv(ROOT / "rr_transition_summary.csv", index=False)

# Motion-labelled intervals from the disruption experiment.
segments = [
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
segment_rows = []
for algorithm in ALGORITHMS:
    frame = aligned["disruption"][algorithm]
    for name, start, end in segments:
        s = frame[frame["algorithm_time_s"].between(start, end)].copy()
        hr = s[
            s["hr_status"].isin(["VALID", "VALID_LOW"])
            & s["hr_reference"].notna()
            & s["hr_bpm"].gt(0)
        ]
        rr = s[
            s["rr_valid"].eq(1)
            & s["rr_reference_10s"].notna()
            & s["rr_bpm"].gt(0)
        ]
        segment_rows.append({
            "algorithm": algorithm,
            "segment": name,
            "start_s": start,
            "end_s": end,
            "rest_measurement_fraction": float(s["rest_state"].eq("REST_MEASUREMENT").mean()),
            "strong_motion_rows": int(s["strong_motion"].fillna(0).sum()),
            "moderate_motion_rows": int(s["motion_class"].eq("MODERATE").sum()) if "motion_class" in s else 0,
            "left_measurement_rows": int(s["left_measurement"].fillna(0).sum()),
            "vital_reset_rows": int(s["vital_reset_since_previous_second"].fillna(0).sum()),
            "hr_accepted_count": len(hr),
            "hr_mae": float((hr["hr_bpm"] - hr["hr_reference"]).abs().mean()) if len(hr) else np.nan,
            "rr_valid_count": len(rr),
            "rr_mae_vs_10s": float((rr["rr_bpm"] - rr["rr_reference_10s"]).abs().mean()) if len(rr) else np.nan,
        })
pd.DataFrame(segment_rows).to_csv(ROOT / "motion_segment_summary.csv", index=False)

# Startup latency after first REST_MEASUREMENT entry.
startup = []
for dataset in DATASETS:
    for algorithm in ALGORITHMS:
        f = aligned[dataset][algorithm]
        entries = f.loc[f["entered_measurement"].eq(1), "algorithm_time_s"]
        entry = float(entries.iloc[0]) if len(entries) else np.nan
        hr = f.loc[f["hr_status"].isin(["VALID", "VALID_LOW"]), "algorithm_time_s"]
        rr = f.loc[f["rr_valid"].eq(1), "algorithm_time_s"]
        first_hr = float(hr.iloc[0]) if len(hr) else np.nan
        first_rr = float(rr.iloc[0]) if len(rr) else np.nan
        startup.append({
            "dataset": dataset,
            "algorithm": algorithm,
            "rest_entry_s": entry,
            "first_accepted_hr_s": first_hr,
            "hr_latency_after_entry_s": first_hr - entry,
            "first_valid_rr_s": first_rr,
            "rr_latency_after_entry_s": first_rr - entry,
        })
pd.DataFrame(startup).to_csv(ROOT / "startup_latency.csv", index=False)

# Static resource estimate from configured windows and Goertzel scan sizes.
resource = pd.DataFrame([
    {
        "algorithm": "Baseline",
        "shared_buffer_seconds": 40,
        "shared_xyz_int16_buffer_bytes": 40 * 25 * 3 * 2,
        "hr_window_seconds": 30,
        "rr_window_seconds": 40,
        "estimate_period_seconds": 2,
        "approx_goertzel_sample_iterations_per_second": int((61*3*750 + 25*3*1000)/2),
    },
    {
        "algorithm": "V14",
        "shared_buffer_seconds": 18,
        "shared_xyz_int16_buffer_bytes": 18 * 25 * 3 * 2,
        "hr_window_seconds": 18,
        "rr_window_seconds": 10,
        "estimate_period_seconds": 1,
        "approx_goertzel_sample_iterations_per_second": int(61*3*450 + 25*3*250),
    },
])
resource.to_csv(ROOT / "resource_estimate.csv", index=False)

# Plots: one figure per dataset and signal.
for dataset in DATASETS:
    baseline = aligned[dataset]["Baseline"]
    v14 = aligned[dataset]["V14"]

    plt.figure(figsize=(12, 5.5))
    plt.plot(v14["algorithm_time_s"], v14["hr_reference"], label="Reference HR", linewidth=2)
    plt.plot(
        baseline["algorithm_time_s"],
        baseline["hr_bpm"].where(baseline["hr_status"].isin(["VALID", "VALID_LOW"])),
        label="Baseline accepted HR",
    )
    plt.plot(
        v14["algorithm_time_s"],
        v14["hr_bpm"].where(v14["hr_status"].isin(["VALID", "VALID_LOW"])),
        label="V14 accepted HR",
    )
    plt.xlabel("Algorithm time (s)")
    plt.ylabel("HR (bpm)")
    plt.title(f"{dataset}: HR reference and accepted output")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS / f"{dataset}_hr.png", dpi=180)
    plt.close()

    plt.figure(figsize=(12, 5.5))
    plt.plot(v14["algorithm_time_s"], v14["rr_reference_10s"], label="10 s RR reference", linewidth=2)
    plt.plot(
        baseline["algorithm_time_s"],
        baseline["rr_bpm"].where(baseline["rr_valid"].eq(1)),
        label="Baseline valid RR",
    )
    plt.plot(
        v14["algorithm_time_s"],
        v14["rr_bpm"].where(v14["rr_valid"].eq(1)),
        label="V14 valid RR",
    )
    plt.xlabel("Algorithm time (s)")
    plt.ylabel("RR (breaths/min)")
    plt.title(f"{dataset}: 10 s RR reference and valid output")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS / f"{dataset}_rr_valid.png", dpi=180)
    plt.close()

    plt.figure(figsize=(12, 5.5))
    plt.plot(v14["algorithm_time_s"], v14["rr_reference_10s"], label="10 s RR reference", linewidth=2)
    plt.plot(v14["algorithm_time_s"], v14["rr_bpm"], label="V14 tracked RR (including invalid)")
    plt.xlabel("Algorithm time (s)")
    plt.ylabel("RR (breaths/min)")
    plt.title(f"{dataset}: RR tracking behaviour and validity gaps")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(PLOTS / f"{dataset}_rr_all_tracked.png", dpi=180)
    plt.close()

print(metrics.round(4).to_string(index=False))
