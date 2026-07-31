# V17 validation report

## Scope

V17 was rebuilt from source and run on all three supplied 100 Hz recordings.
Each output was aligned to reference time `algorithm_time_s - 1`.  Instantaneous
reference and an 18 s trailing HR reference are both reported because the
estimator uses an 18 s causal window.  No reference values enter the estimator.

All three recordings influenced development.  The results below are regression
evidence, not an independent estimate of generalisation.

## Reportable HR versus instantaneous reference

| Recording | Version | Coverage | MAE (bpm) | Bias (bpm) | Correlation | >8 bpm rows | Longest >8 bpm run |
|---|---|---:|---:|---:|---:|---:|---:|
| stable | V15 | 99.5% | 3.43 | +0.27 | 0.481 | 23 | 12 s |
| stable | V17 | 99.5% | 3.23 | +0.44 | 0.530 | 21 | 12 s |
| RR change | V15 | 99.5% | 3.99 | -1.12 | 0.610 | 27 | 11 s |
| RR change | V17 | 99.5% | 3.76 | -0.77 | 0.602 | 25 | 10 s |
| disruption | V15 | 80.9% | 5.33 | +4.10 | 0.580 | 35 | 11 s |
| disruption | V17 | 77.0% | 5.24 | +4.07 | 0.598 | 28 | 11 s |

V17 lowers reportable MAE on all three recordings.  Stable and disruption
correlation rise.  RR-change correlation decreases slightly (0.008) while MAE,
bias, five-second direction agreement, and longest large-error run improve.

The lower disruption coverage is intentional: raising `VALID_LOW` from 45 to
50 removes low-confidence rows.  Moderate motion remains fully suppressed.

## Causal-window-aware comparison

Against the 18 s trailing reference:

| Recording | V15 MAE | V17 MAE | V15 correlation | V17 correlation |
|---|---:|---:|---:|---:|
| stable | 2.73 | 2.63 | 0.695 | 0.722 |
| RR change | 3.06 | 2.96 | 0.824 | 0.811 |
| disruption | 4.81 | 4.77 | 0.585 | 0.631 |

A lag-only diagnostic (no amplitude calibration) also shows that the output
amplitude ratio moves closer to reference for stable (1.401 to 1.374) and RR
change (1.114 to 1.069).  Disruption remains effectively unchanged (1.430 to
1.428).  This is evidence of reduced amplitude exaggeration in two recordings,
not proof of calibration.

## Strict accepted HR

| Recording | Version | Count | MAE | Bias | Correlation |
|---|---|---:|---:|---:|---:|
| stable | V15 | 181 | 2.99 | +0.51 | 0.573 |
| stable | V17 | 191 | 3.05 | +0.19 | 0.552 |
| RR change | V15 | 186 | 3.86 | -1.31 | 0.642 |
| RR change | V17 | 186 | 3.64 | -0.89 | 0.619 |
| disruption | V15 | 118 | 5.17 | +4.06 | 0.621 |
| disruption | V17 | 102 | 5.12 | +4.23 | 0.673 |

Stable accepted MAE rises by 0.06 bpm while coverage rises by ten rows and bias
falls.  This small regression is retained rather than hidden by another
dataset-specific threshold.

## SQI reliability

At SQI >= 75, the fraction of rows with >8 bpm error changes as follows:

| Recording | V15 | V17 |
|---|---:|---:|
| stable | 4.92% | 1.79% |
| RR change | 7.35% | 3.51% |
| disruption | 12.00% | 8.70% |

At SQI >= 80, neither V15 nor V17 has a >8 bpm error in the supplied data.
V17 therefore improves the practical meaning of high SQI without claiming that
the threshold is externally calibrated.

## Motion regression and local exceptions

- 69–100 s low-amplitude sway remains in `REST_MEASUREMENT`; V17 HR MAE changes
  from 4.78 to 4.37 bpm.
- 156–159 s moderate motion has zero reportable HR rows in both versions.
- 202–204 s severe motion exits REST and resets the estimator in both versions.
- 160–201 s recovery remains the main unresolved local error.  V17 reduces MAE
  from 8.44 to 7.47 bpm but still follows a persistent high mechanical band.
- Post-reset HR remains low by about 7.5 bpm in the five available reportable
  rows; V17 does not improve this very short segment.
- The longest continuous >8 bpm reportable error is not eliminated: 12 s in
  stable and 11 s in disruption.

These residuals are why V17 should be treated as a proposed version pending an
unseen recording, not as proven ground-truth HR extraction.

## RR and rest regression

Across 36 compared RR/rest/motion output columns and all three recordings,
V15 and V17 have zero mismatched rows at `1e-6` numeric tolerance.  HR work did
not change RR, rest entry/exit, motion classification, or reset behaviour.

## Resource effect

- Shared 18 s XYZ `int16_t` vital buffer: unchanged at 2700 bytes.
- `VitalEstimator_t`: 2932 bytes in V15 and 3140 bytes in the validation build
  of V17 (+208 bytes).
- `VitalOutput_t`: 164 to 328 bytes because PC validation exports extensive
  diagnostics.  Nonessential diagnostic fields may be removed from the final
  STM32/BLE build.
- On the host validation build, 50 stable-recording runs took 8.47 s for V15
  and 14.16 s for V17.  This is a relative CPU regression test, not an STM32
  timing estimate.  STM32 cycle timing and stack high-water measurement are
  required before porting is accepted.

## Files

- `validation/metrics_summary_v17.csv`
- `validation/hr_sqi_calibration_v17.csv`
- `validation/hr_lag_diagnostic_v17.csv`
- `validation/motion_segment_summary_v17.csv`
- `validation/rr_rest_invariance_v17.csv`
- `validation/hr_evidence_usage_v17.csv`
- `validation/plots_v17/`
