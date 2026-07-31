#!/usr/bin/env python3
from pathlib import Path
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parent
DATASETS = {
    "stable": {"hr_col": "HR"},
    "rr_change": {"hr_col": "HR"},
    "disruption": {"hr_col": "Unnamed: 11"},
}

def safe_corr(a, b):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    if len(a) < 3 or np.std(a) == 0 or np.std(b) == 0:
        return np.nan
    return float(np.corrcoef(a, b)[0, 1])

def align(dataset, filename):
    folder = ROOT / "datasets" / dataset
    out = pd.read_csv(folder / filename)
    ref = pd.read_csv(folder / "reference.csv").set_index("time_s")
    out["reference_time_s"] = (out["algorithm_time_s"] - 1).round().astype(int)
    out["hr_reference"] = out["reference_time_s"].map(ref[DATASETS[dataset]["hr_col"]])
    out["rr_reference_10s"] = out["reference_time_s"].map(ref["rr_10s_trailing_bpm"])
    return out

def evaluate(frame, mode):
    if mode == "HR_ACCEPTED":
        mask = frame["hr_status"].isin(["VALID", "VALID_LOW"])
        pred, ref = "hr_bpm", "hr_reference"
    elif mode == "HR_REPORTABLE":
        mask = frame["hr_reportable"].eq(1)
        pred, ref = "hr_bpm", "hr_reference"
    elif mode == "RR_VALID":
        mask = frame["rr_valid"].eq(1)
        pred, ref = "rr_bpm", "rr_reference_10s"
    elif mode == "RR_REPORTABLE":
        mask = frame["rr_reportable"].eq(1)
        pred, ref = "rr_bpm", "rr_reference_10s"
    else:
        raise ValueError(mode)

    mask &= frame[pred].gt(0) & frame[ref].notna()
    error = frame.loc[mask, pred] - frame.loc[mask, ref]
    return {
        "mode": mode,
        "count": int(mask.sum()),
        "mae": float(error.abs().mean()),
        "bias": float(error.mean()),
        "correlation": safe_corr(frame.loc[mask, pred], frame.loc[mask, ref]),
    }

rows = []
for dataset in DATASETS:
    frame = align(dataset, "v15_results.csv")
    for mode in ["HR_ACCEPTED", "HR_REPORTABLE", "RR_VALID", "RR_REPORTABLE"]:
        rows.append({"dataset": dataset, **evaluate(frame, mode)})

result = pd.DataFrame(rows)
result.to_csv(ROOT / "metrics_rebuilt_v15.csv", index=False)
print(result.round(4).to_string(index=False))
