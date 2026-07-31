# Algorithm changes from V17 to V18

## 1. Recent-window evidence reweighting

V17 allowed a candidate-local peak to dominate recent consistency even when
the recent window's global peak had moved elsewhere.  That made a stale
high-SNR path look current.

The recent-consistency weights changed from:

- candidate-local presence: 0.65 to 0.35;
- local-frequency agreement: 0.25 to 0.15;
- recent global-frequency agreement: 0.10 to 0.50.

This is the main SQI feature change that rejects the old 90–97 bpm path around
stable 130 s while supporting the 76–80 bpm candidate already in slots 2/3.

## 2. Cardiac structural evidence reweighting

Candidate structural quality changed from:

| Component | V17 | V18 |
|---|---:|---:|
| half-window consistency | 0.30 | 0.22 |
| recent-window consistency | 0.30 | 0.30 |
| peak shape/local background | 0.20 | 0.18 |
| multi-axis support | 0.10 | 0.12 |
| normalized autocorrelation periodicity | 0.10 | 0.18 |

Final candidate evidence changed from 78% transition spectral quality plus 22%
structure to 64% transition quality plus 36% structure.  SNR and prominence
remain important but cannot dominate the challenger decision as easily.

## 3. Independent stale-path release decision

V17 required a distant challenger to become candidate 1 three times.  V18
retains that normal transition route and adds an independent release route.

A challenger may release the old track after two supported appearances, with
one missing frame tolerated, only when all of the following hold:

- the candidate is lower than the independent spectral anchor;
- it is at least 10 bpm above the configured HR lower boundary;
- the recent global frequency is within 6 bpm of the challenger;
- the recent global frequency rejects the anchor by at least 8 bpm;
- evidence quality is at least 0.50;
- recent consistency is at least 0.68;
- half-window consistency is at least 0.58;
- periodicity is at least 0.28 or multi-axis support is at least 0.55;
- challenger evidence is not materially worse than the anchor candidate;
- accumulated challenger evidence reaches 1.05.

Evidence is accumulated across all top-three candidate slots with 4 bpm path
matching, 0.82 decay, and one-frame gap tolerance.  Therefore an interruption
or a 77/78/80 bpm position change does not erase the path.

## 4. Spectral-anchor-based contradiction

Release decisions compare evidence with `hr_anchor_track_bpm`, not the
temporarily rate-limited user-facing output.  This prevents one completed
release from immediately launching another release merely because the output
is still displaced from the conservative anchor.

## 5. Release latch and exit behavior

A confirmed release remains active while the challenger is supported, even
after the user-facing output comes within 8 bpm.  This prevents the old path
from immediately reclaiming the output.

The latch ends when:

- the independent spectral anchor converges within 4 bpm;
- the challenger is absent for more than one estimate; or
- repeated recent/structural contradiction consumes the accumulated release
  support.

## 6. Dynamic HR rate limits

The normal HR limit remains 2 bpm per one-second estimate.  V18 uses:

- up to 3 bpm/s for a conservatively confirmed primary transition;
- up to 4 bpm/s for a structurally confirmed stale-path release.

The release update uses a 0.55 target gain; other confirmed transitions use
0.42.  RR remains unchanged at 2 brpm/s.

## 7. Final HR SQI composition

The V18 evidence factor is:

- 0.54 base;
- 0.11 half-window consistency;
- 0.13 recent-window consistency;
- 0.07 peak shape;
- 0.06 periodicity;
- 0.03 candidate margin;
- 0.03 axis stability;
- 0.03 historical track support.

Output-to-spectrum agreement is no longer a small additive reward.  It is a
multiplicative trajectory factor from 0.60 to 1.00.  Persistent HR–RR
suspicion is then subtracted with weight 0.10 and motion quality is applied
last.  This prevents stable history from masking a currently contradicted
output.

Because the revised SQI is intentionally more conservative and has a different
scale, `VALID_LOW` was recalibrated from 50 to 45.  At 45, V18 retains nearly
full reportable coverage on stable and RR-change while improving large-error
rates on all three supplied recordings.

## 8. Preserved behavior

- RR candidate search, SQI, tracker, and rate limit are unchanged.
- Rest detector and motion thresholds are unchanged.
- 100 Hz input and 4:1 decimation are unchanged.
- HR/RR windows and one-second cadence are unchanged.
- Moderate motion still suppresses output and severe motion still exits REST
  and resets the estimator.
- No reference values, time-specific rules, fixed offset, or amplitude
  calibration are used by the estimator.

## 9. Variants tested and rejected

- **Unconditional evidence-winner selection:** fixed stable 130 s but degraded
  RR-change and disruption because structurally attractive mechanical peaks
  sometimes outranked candidate 1.
- **Release in both directions:** switched upward to 94/106/120 bpm mechanical
  bands during disruption.
- **Release near the HR lower boundary:** selected a persistent 64–65 bpm
  sub-path in RR-change.
- **Clearing release when output came within 8 bpm:** caused the old high path
  to rebound before the anchor caught up.
- **Keeping `VALID_LOW` at 50:** improved confidence purity but unnecessarily
  reduced reportable coverage after the SQI scale changed.
