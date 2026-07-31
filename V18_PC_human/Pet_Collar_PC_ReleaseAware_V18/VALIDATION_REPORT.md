# Historical V15 validation report

This report is retained as the V15 baseline.  The current comparison is in
`VALIDATION_REPORT_V18.md`.

## Why V15 was needed

V14's internal HR/RR tracks were often smooth during genuine changes, but the plotted accepted/valid curves contained gaps. The gap was created by the confidence layer:

- a new frequency candidate appeared;
- the output tracker intentionally moved toward it gradually;
- candidate-output disagreement reduced SQI;
- the result disappeared until the rate became stable again.

This is unsuitable for abnormal-trend detection.

## Key RR-change results

Using the complete estimable interval:

| Output definition | Coverage | MAE | Correlation |
|---|---:|---:|---:|
| V14 accepted HR | 86.9% | 3.57 bpm | 0.520 |
| V15 accepted HR | 90.3% | 3.86 bpm | 0.642 |
| V15 reportable HR | 99.5% | 3.99 bpm | 0.610 |
| V14 valid RR | 81.6% | 0.56 brpm | 0.987 |
| V15 valid RR | 85.0% | 0.69 brpm | 0.971 |
| V15 reportable RR | 96.6% | 0.85 brpm | 0.956 |

The reportable curve intentionally accepts a modest error increase in exchange for preserving the rise/fall trajectory. Strict-valid fields remain available when higher confidence is required.

## Smoothness

- HR reportable maximum adjacent change remains 2.0 bpm/s.
- RR reportable 95th-percentile adjacent change is about 0.63 brpm/s.
- Isolated one-second candidate jumps still cannot immediately redirect the track.

## Motion regression

In the disruption recording:

- 69–100 s low-amplitude sway: REST is retained; reportability depends on current signal evidence.
- 156–159 s moderate motion: HR and RR reportability are both zero.
- 202–204 s severe motion: REST is exited and the estimator is reset.

Thus trend reporting does not bypass the motion gates.

## SQI safety check

Across the three supplied recordings, no V15 output with SQI at least 80 exceeded:

- 8 bpm HR absolute error; or
- 5 brpm RR absolute error.

This does not prove external calibration, but the new trend path did not create high-SQI gross errors in the regression data.

## Trade-off

The reportable curve is the correct curve for monitoring and anomaly detection. The strict-valid curve is the correct curve for high-confidence summary statistics.

Using only strict-valid outputs will always bias the visible curve toward stable periods, because uncertainty is greatest during transitions.
