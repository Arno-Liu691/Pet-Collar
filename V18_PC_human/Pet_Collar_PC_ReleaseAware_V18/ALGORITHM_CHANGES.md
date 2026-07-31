# Historical V14 to V15 algorithm changes

This file is retained for version history.  For the current V18 changes, see
`ALGORITHM_CHANGES_V17_TO_V18.md`; the preceding evidence features are recorded
in `ALGORITHM_CHANGES_V15_TO_V17.md`.

## 1. Corrected HR transition evidence

V14 confirmed a distant HR candidate using the ordinary HR SQI. That SQI already contained a continuity penalty, so a real rate change was penalised precisely because it differed from the old track.

V15 uses the separate `transition_sqi`, which excludes temporal continuity, for distant HR switch confirmation. This matches the RR design and allows a persistent new HR peak to move the tracker.

## 2. Persistent trend confirmation

- HR: a distant candidate must remain consistent for 3 consecutive one-second estimates.
- RR: a distant candidate must remain consistent for 2 estimates.
- Candidates must remain within the existing pending-candidate tolerance and satisfy transition-quality requirements.
- A single isolated jump therefore remains rejected.

## 3. Transition-aware SQI

When a transition is confirmed, SQI is calculated mainly from transition evidence rather than from output-to-candidate proximity.

This removes the V14 failure mode in which a deliberately smoothed output was marked unreliable simply because it had not yet reached the new candidate frequency.

Harmonic relationships remain graded SQI evidence. They do not directly force a validity state.

## 4. Strict validity separated from reportability

New fields:

- `hr_reportable`
- `hr_trend_active`
- `rr_reportable`
- `rr_trend_active`

Strict `valid` fields are retained. Reportability exists so that genuine trends remain observable without pretending that every transitional estimate has stable-state confidence.

## 5. Short confidence bridge

A reportable result is retained for up to 4 seconds after recent valid/confirmed-trend evidence, provided:

- a track exists;
- there is enough RR energy where applicable;
- motion is below `MODERATE`;
- the rate still changes only through the normal limiter.

This bridges brief one- or two-second spectral-quality dips. It does not bridge moderate or severe motion.

## 6. HR status `TREND`

`TREND` means a persistent change or short trend-confidence bridge is being reported. It is not equivalent to `VALID`.

## 7. Motion behaviour preserved

- Low-amplitude sway may remain reportable if the track remains plausible.
- Moderate motion clears reportability and produces HR `CONTAM`.
- Severe motion still exits REST and resets the estimator.

## 8. What was deliberately not changed

- HR 18 s and RR 10 s windows
- 1 s output cadence
- 2 bpm/brpm per-second absolute rate limits
- Goertzel spectral search
- REST state-machine structure
- provisional human motion thresholds

The V15 change is structural rather than a dataset-specific threshold fit.
