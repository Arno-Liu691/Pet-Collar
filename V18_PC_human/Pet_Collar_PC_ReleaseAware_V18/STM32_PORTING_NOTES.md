# STM32 porting notes for proposed V18

Port the four algorithm files:

- `algorithm/rest_detector.c`
- `algorithm/rest_detector.h`
- `algorithm/vital_estimator.c`
- `algorithm/vital_estimator.h`

## Memory

The 18 s, 25 Hz, three-axis `int16_t` vital ring remains 2700 bytes.  The host
build reports `VitalEstimator_t` increasing from 2932 to 3140 bytes (+208).
`VitalOutput_t` increases from 164 to 328 bytes because V18 exports detailed PC
validation diagnostics.  Before STM32 integration, place optional diagnostics
behind a compile-time flag if stack/RAM margin is tight.

No raw 100 Hz long window, gyro long window, dynamic allocation, or duplicate
acceleration window was added.

## CPU

V18 enriches only the top three HR candidates.  It retains candidate-period ACF,
two 9 s spectral checks, and one recent 8 s spectral check.  Host regression is
about 1.67x V15 runtime.  Measure M4 cycle time around
`VitalEstimator_Estimate()` and verify the one-second deadline with BLE active.

The V18 release decision reuses these features and adds no spectral pass. If
optimisation is necessary, preserve the evidence definitions but reuse
precomputed half/recent spectra across candidates before removing an evidence
class.

## BLE semantics

Recommended payload:

- HR value, SQI, status, reportable flag;
- RR value, SQI, strict-valid flag, reportable flag;
- optional compact debug bits: consensus used, trend active, motion class.

Do not transmit the large PC diagnostic structure over BLE.  `TREND` means a
persistent rate change is being followed; it is not equivalent to `VALID`.

## Required STM32 checks

1. Re-run PC/MCU numerical consistency with the new ACF and segment Goertzel
   functions.
2. Measure stack high-water mark and estimator cycle time.
3. Confirm FIFO overrun/unmatched-pair counters remain clean.
4. Repeat the three recordings through the MCU build and compare every common
   output field.
5. Validate on a new unseen human recording before changing the accepted V15
   baseline.

The normal HR limiter remains 2 bpm/s, but a confirmed primary transition can
use 3 bpm/s and a contradicted-path release can use 4 bpm/s.  Verify that the
downstream display/BLE consumer does not assume a fixed 2 bpm/s HR delta.  RR
remains limited to 2 brpm/s.

Human HR/RR ranges and all motion thresholds remain provisional for canine
deployment.
