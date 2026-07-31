# V18 validation report

## Scope

V18 was rebuilt from source and run on all three supplied 100 Hz recordings.
Outputs are aligned to reference time `algorithm_time_s - 1`.  No reference
value is used by the estimator.  All three recordings influenced development,
so these results are regression evidence rather than an independent estimate
of generalisation.

## Reportable HR versus instantaneous reference

| Recording | Version | Coverage | MAE | Bias | Correlation | >8 bpm rows | Longest >8 run |
|---|---|---:|---:|---:|---:|---:|---:|
| stable | V15 | 99.5% | 3.43 | +0.27 | 0.481 | 23 | 12 s |
| stable | V17 | 99.5% | 3.23 | +0.44 | 0.530 | 21 | 12 s |
| stable | V18 | 99.5% | 2.58 | -0.33 | 0.632 | 10 | 8 s |
| RR change | V15 | 99.5% | 3.99 | -1.12 | 0.610 | 27 | 11 s |
| RR change | V17 | 99.5% | 3.76 | -0.77 | 0.602 | 25 | 10 s |
| RR change | V18 | 98.1% | 3.50 | -0.91 | 0.653 | 21 | 10 s |
| disruption | V15 | 80.9% | 5.33 | +4.10 | 0.580 | 35 | 11 s |
| disruption | V17 | 77.0% | 5.24 | +4.07 | 0.598 | 28 | 11 s |
| disruption | V18 | 69.7% | 4.91 | +4.24 | 0.752 | 21 | 5 s |

V18 lowers reportable MAE and large-error count on every supplied recording.
The disruption improvement partly comes from more conservative SQI: persistent
high mechanical-band rows are less often claimed as reportable.

## Target stable failure

| Segment | Version | MAE | Bias | Maximum error | >8 bpm rows |
|---|---|---:|---:|---:|---:|
| 121–130 s | V15 | 6.27 | +5.86 | 11.48 | 4 |
| 121–130 s | V17 | 7.62 | +7.62 | 10.33 | 4 |
| 121–130 s | V18 | 5.59 | +5.34 | 8.81 | 2 |
| 131–140 s | V15 | 11.29 | +11.29 | 13.83 | 8 |
| 131–140 s | V17 | 10.92 | +10.92 | 13.35 | 8 |
| 131–140 s | V18 | 2.14 | +0.54 | 4.81 | 0 |
| 121–140 s | V17 | 9.27 | +9.27 | 13.35 | 12 |
| 121–140 s | V18 | 3.86 | +2.94 | 8.81 | 2 |

At 131 s, V18 releases the old high path to the accumulated 79.4 bpm
challenger and uses the 4 bpm/s contradicted-path limit.  Output falls from
88.81 bpm at 130 s to 84.81, 81.44, and 79.81 bpm over the next three rows.
V17 instead remains above 91 bpm through 133 s.

V18 slightly undershoots during the later recovery because the causal window
continues to support 74–76 bpm while the instantaneous reference starts rising.
The 141–155 s MAE is nevertheless 1.72 bpm versus 2.66 bpm in V17, and there
are no >8 bpm rows in that recovery segment.

## Strict accepted HR

| Recording | Version | Count | Coverage | MAE | >8 bpm rows |
|---|---|---:|---:|---:|---:|
| stable | V17 | 191 | 92.7% | 3.05 | 16 |
| stable | V18 | 179 | 86.9% | 2.35 | 6 |
| RR change | V17 | 186 | 90.3% | 3.64 | 20 |
| RR change | V18 | 165 | 80.1% | 3.35 | 13 |
| disruption | V17 | 102 | 57.3% | 5.12 | 23 |
| disruption | V18 | 87 | 48.9% | 4.57 | 15 |

The revised SQI is more selective.  `VALID_LOW` was recalibrated from 50 to 45
after testing both thresholds.  Keeping 50 reduced reportable coverage to
95.6%, 92.2%, and 61.8%; using 45 restores 99.5%, 98.1%, and 69.7% while still
improving error reliability over V17.

## SQI reliability

At SQI >= 75, V18 has no >8 bpm rows in any supplied recording:

| Recording | Rows | MAE | >8 bpm fraction |
|---|---:|---:|---:|
| stable | 29 | 1.31 | 0% |
| RR change | 28 | 3.73 | 0% |
| disruption | 8 | 0.50 | 0% |

Counts at high SQI are smaller than V17 because output-to-spectrum agreement is
now multiplicative and stale history has less weight.  These thresholds remain
internal regression calibration, not externally validated confidence levels.

## Causal-window and lag diagnostics

Against the 18 s trailing reference, stable reportable MAE improves from 2.63
to 2.04 bpm and correlation from 0.722 to 0.770.  RR-change trailing-reference
MAE changes from 2.96 to 3.21 and disruption from 4.77 to 4.85; instantaneous
reference metrics improve for both.  This is an explicit tradeoff of releasing
the stale path earlier rather than maximising similarity to the full 18 s
average.

The best correlation-only lag on stable falls from 11 s in V17 to 8 s in V18,
and its amplitude ratio moves from 1.374 to 1.105.  No amplitude calibration or
offset was applied.

## All tracked HR

SQI filtering is not the only source of improvement:

| Recording | V17 all-track MAE | V18 all-track MAE | V17 correlation | V18 correlation |
|---|---:|---:|---:|---:|
| stable | 3.23 | 2.58 | 0.530 | 0.631 |
| RR change | 3.76 | 3.61 | 0.602 | 0.642 |
| disruption | 6.20 | 5.87 | 0.538 | 0.582 |

The disruption recovery still has a 25 s all-track >8 bpm run.  V18 does not
solve that persistent mechanical-band source separation; it mostly labels the
interval as low confidence.

## RR, rest, and motion invariance

Across the compared RR/rest/motion fields and all three recordings, V17 and
V18 have zero mismatched rows at `1e-6` numeric tolerance.  Moderate motion
remains suppressed, severe motion still exits measurement and resets the
estimator, and RR calculations are unchanged.

## Resource effect

- Shared 18 s XYZ `int16_t` ring: unchanged at 2700 bytes.
- `VitalEstimator_t`: 3140 bytes, unchanged in host `sizeof` from V17 because
  the added release bytes occupy existing alignment/padding.
- `VitalOutput_t`: 328 bytes, unchanged.
- V18 reuses the V17 candidate features; release logic adds no new spectral or
  autocorrelation pass.

STM32 cycle time, compiler-specific layout, and stack high-water mark still
require measurement on the target build.

## Generated validation artifacts

- `validation/metrics_summary_v18.csv`
- `validation/hr_local_failure_summary_v18.csv`
- `validation/hr_sqi_calibration_v18.csv`
- `validation/hr_lag_diagnostic_v18.csv`
- `validation/motion_segment_summary_v18.csv`
- `validation/rr_rest_invariance_v18.csv`
- `validation/hr_evidence_usage_v18.csv`
- `validation/plots_v18/`
