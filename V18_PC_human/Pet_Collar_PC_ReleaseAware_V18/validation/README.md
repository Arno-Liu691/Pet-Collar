# Validation directory

Each dataset folder contains the 100 Hz input, reference, historical baseline
results, V15/V17 results, and rebuilt V18 results.

Run the current regression from the project root:

```bash
bash run_validation.sh
```

Primary V18 outputs:

- `metrics_summary_v18.csv`: V15/V17/V18 accuracy, coverage, bias, smoothness, trend, and
  continuous-error metrics;
- `hr_local_failure_summary_v18.csv`: explicit stable 121–155 s regression;
- `hr_sqi_calibration_v18.csv`: SQI thresholds versus error;
- `hr_lag_diagnostic_v18.csv`: lag-only trend/amplitude diagnostic;
- `motion_segment_summary_v18.csv`: disruption intervals;
- `rr_rest_invariance_v18.csv`: V17/V18 RR and rest equality check;
- `hr_evidence_usage_v18.csv`: consensus, axis, and coupling diagnostics;
- `plots_v18/`: V15/V17/V18 reference, SQI, error, and candidate plots.

The evaluator maps algorithm time `t` to reference time `t-1`.  The 18 s
trailing reference is diagnostic only and does not enter the algorithm.
