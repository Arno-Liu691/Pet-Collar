# Complete algorithm changes from V15 to proposed V17

## 1. HR evidence features

### Candidate superiority

The top three HR candidates now retain their transition SQI and new structural
evidence.  V17 calculates the difference between the strongest and second
strongest evidence.  A clear margin keeps the original primary candidate;
small margins mark an ambiguous spectrum and reduce SQI.

### Recent-versus-long-window support

For each of the top three candidates, V17 evaluates the recent 8 s window on
the candidate axis.  Candidate-local power is compared with the strongest
recent HR-band power.  This tests support for the same frequency or a nearby
trend without requiring the exact 18 s peak to remain unchanged.

### First-half versus second-half support

The 18 s HR window is split into two 9 s halves.  Each candidate receives a
score combining:

- local-frequency agreement between halves;
- agreement of the two half-window peaks with the long-window candidate;
- energy balance between halves;
- candidate-local power relative to the strongest HR-band power in each half.

### Time-domain periodicity

Normalized autocorrelation is evaluated at the candidate period and twice the
candidate period, with +/-1 sample lag tolerance.  The one-period and
two-period terms reduce the chance that an RR harmonic looks periodic at only
one short lag.  ACF is graded evidence, never a hard acceptance gate.

### Peak shape and local background

The former adjacent-bin prominence is retained.  V17 also estimates local
background outside the immediate peak and the number of nearby bins above half
power.  This penalises a high-SNR peak that is broad or sits on a poor local
background.

## 2. Candidate path selection

### Primary spectral anchor

V15's primary-candidate tracker is retained as an internal anchor.  Candidate
continuity in the next search is calculated against this anchor rather than
the fused user-facing output.  This prevents consensus decisions from changing
the next candidate ranking and reinforcing themselves.

### Ambiguity-aware consensus

If candidate evidence differs by less than 0.06 and no confirmed transition is
active, candidates within 12 bpm of the current track are combined with
evidence weights.  The result is a tracking target, not a claim that a new
physical spectral peak exists.  A clear primary candidate bypasses consensus.

### Gap-tolerant challenger

V15 required a distant candidate to appear on consecutive estimates.  V17
accumulates evidence for the same frequency path across all top-three slots:

- matching tolerance: 4 bpm;
- evidence decay: 0.82 per estimate;
- at most one intervening frame;
- three recent appearances as the primary candidate;
- current primary evidence at least 0.55;
- clear primary-versus-alternative evidence margin;
- challenger evidence must also exceed 70% of current-track support.

Thus candidate-2/3 history can prepare a real takeover, but a weak alternative
cannot take control without becoming the primary peak.  A confirmed path may
survive one missing frame while the output remains rate limited.

## 3. HR history, axis, and HR–RR state

- Current-band support is accumulated from any of the top three candidates
  within 6 bpm of the current/transition band.
- The selected axis has an age counter.  A stable single axis can remain fully
  credible even when cross-axis support is low.
- A sudden switch to an axis with poor cross-axis support receives a temporary
  penalty.
- HR–RR harmonic suspicion is an exponential history, strengthened by high RR
  confidence, a shared axis, weak HR periodicity, and a low instantaneous
  harmonic-quality score.  It lowers SQI but never directly invalidates HR.

## 4. Final SQI composition

V17 initially tested an additive SQI.  It was rejected because several
mediocre components could sum to an overconfident result.

The final implementation uses V15 candidate SQI as the base.  During a
confirmed transition, transition evidence is blended into that base.  The base
is then multiplied by a factor containing:

- structural evidence;
- accumulated current-band history;
- candidate separation;
- axis stability;
- output-to-spectrum agreement.

Persistent HR–RR suspicion is subtracted afterwards, and motion quality is
applied last.  `VALID_LOW` was raised from SQI 45 to 50 after regression showed
that the four-second report hold preserves the stable/RR-change trend while
removing low-confidence disruption rows.

## 5. Diagnostics and CSV output

The PC output now includes, for all top-three candidates:

- SQI and transition SQI;
- axis, prominence, cross-axis support, and harmonic quality;
- periodicity, half-window consistency, recent-window consistency, peak shape,
  and recent best frequency.

Selected-path output additionally includes candidate margin, selected evidence
components, history quality, axis stability, output agreement, persistent
HR–RR suspicion, challenger state, and whether consensus was used.

## 6. Preserved behaviour

- RR estimator and RR tracker are unchanged.
- Rest detector and motion thresholds are unchanged.
- 100 Hz input and 4:1 decimation are unchanged.
- HR/RR windows and one-second cadence are unchanged.
- HR/RR output change remains limited to 2 units per second.
- Moderate motion still suppresses reportability; strong motion still exits
  REST and resets the estimator.

## 7. Investigated but rejected approaches

- **ACF as a hard selector:** improved the stable recording but selected wrong
  paths during disruption.
- **Replacing the primary peak with a history-only multi-path winner:** reduced
  bias on one recording but damaged the genuine HR rise/fall correlation on
  the RR-change recording.
- **Feeding consensus back into candidate continuity:** created positive
  feedback and large erroneous track changes in the C implementation.
- **Additive SQI:** produced too many high-SQI wrong outputs.
- **Fixed HR offset or amplitude calibration:** rejected as recording-specific
  overfitting and unsuitable for later canine deployment.
