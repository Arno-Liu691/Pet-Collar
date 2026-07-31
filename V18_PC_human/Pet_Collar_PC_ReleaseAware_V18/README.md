# Pet Collar PC Release-Aware Algorithm V18

V18 continues the V17 HR work while preserving the accepted V15 RR estimator,
rest detector, motion policy, 18 s HR window, 10 s RR window, and one-second
cadence.  Its main purpose is to release a stale high-HR path when a lower
cardiac candidate is already present in candidate slots 2 or 3.

## Core design

V18 keeps two HR states:

1. **Primary spectral anchor** — the conservative V15-style peak tracker used
   for spectral continuity and as an independent reference for old-path
   contradiction.
2. **User-facing HR track** — ambiguity-aware consensus plus a multi-slot
   challenger tracker that can release a contradicted high path without first
   requiring the challenger to become candidate 1 three times.

The fast release route is deliberately asymmetric.  It is available only for
a lower candidate, at least 10 bpm above the configured HR lower boundary,
that has two supported appearances with one-frame gap tolerance.  The recent
8 s global peak must support the challenger and reject the anchor path; half
window consistency plus periodicity or multi-axis support must also be
credible.  Upward changes and near-boundary candidates retain the conservative
primary-candidate confirmation path.

## HR SQI evidence

The final HR SQI checks:

- candidate superiority over candidates 2 and 3;
- multi-axis support or persistent support on one stable axis;
- 18 s versus recent 8 s spectral support;
- first-half versus second-half frequency and energy consistency;
- normalized autocorrelation at one and two candidate periods;
- peak width and local-background contrast;
- agreement between the output trajectory and current spectral target;
- accumulated support for the current HR band;
- persistent HR–RR harmonic suspicion;
- sudden candidate-axis changes;
- motion context.

V18 gives substantially more weight to the recent window's global frequency,
periodicity, half-window consistency, and peak shape.  Historical continuity
and output agreement can no longer keep a stale high-SNR path confident.
Output agreement is multiplicative, so a rate-limited value far from current
spectral evidence receives low SQI while it catches up.

## Configuration

- Input: 100 Hz
- Vital storage: 25 Hz after 4:1 decimation
- HR window: 18 s
- RR window: 10 s
- Estimate period: 1 s
- Normal HR rate limit: 2 bpm per estimate
- Confirmed primary HR transition limit: 3 bpm per estimate
- Contradicted-path HR release limit: 4 bpm per estimate
- RR rate limit: unchanged at 2 brpm per estimate
- HR challenger matching tolerance: 4 bpm, with one-frame gap tolerance
- HR `VALID`: SQI >= 65
- HR `VALID_LOW`: SQI >= 45 after V18 SQI rescaling
- Report hold: 4 s

## Build and run

Linux:

```bash
bash build_linux.sh
./pet_collar_pc_v18 input.csv output.csv
```

Windows / MinGW:

```bat
build_windows.bat
pet_collar_pc_v18.exe input.csv output.csv
```

Full supplied-data regression:

```bash
bash run_validation.sh
```

## Validation summary

Reportable HR versus the instantaneous reference:

| Recording | V15 MAE | V17 MAE | V18 MAE | V18 coverage | V17 >8 rows | V18 >8 rows |
|---|---:|---:|---:|---:|---:|---:|
| stable | 3.43 | 3.23 | 2.58 | 99.5% | 21 | 10 |
| RR change | 3.99 | 3.76 | 3.50 | 98.1% | 25 | 21 |
| disruption | 5.33 | 5.24 | 4.91 | 69.7% | 28 | 21 |

The motivating stable 131–140 s failure changes from 10.92 bpm MAE in V17 to
2.14 bpm in V18.  Its eight >8 bpm rows fall to zero and maximum error falls
from 13.35 to 4.81 bpm.

All compared RR and rest/motion fields are numerically identical between V17
and V18.  See `VALIDATION_REPORT_V18.md`,
`ALGORITHM_CHANGES_V17_TO_V18.md`, and `validation/plots_v18/`.

## Interpretation and limits

All three recordings influenced development and are regression data, not an
independent holdout.  V18 uses no reference values, fixed HR offset, amplitude
calibration, or recording-specific time rule inside the estimator.

The disruption recovery interval still follows a persistent high mechanical
band.  V18 lowers its SQI and suppresses many such rows rather than claiming a
reliable cardiac rate; it does not solve that source-separation problem.  The
human profile also requires an unseen human recording before acceptance and
animal-labelled validation before canine HR limits are enabled.
