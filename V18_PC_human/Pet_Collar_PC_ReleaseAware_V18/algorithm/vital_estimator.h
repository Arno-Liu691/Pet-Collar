#ifndef VITAL_ESTIMATOR_H
#define VITAL_ESTIMATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "rest_detector.h"

/* ============================================================
 * Vital estimator configuration - V18 release-aware HR candidate
 * ============================================================
 * - 100 Hz input, decimated by 4 to 25 Hz.
 * - 18 s HR window and 10 s RR window to reduce latency.
 * - One estimate per second.
 * - Candidate quality combines SNR, local peak prominence,
 *   multi-axis support, temporal continuity, harmonic ambiguity,
 *   and motion context.
 * - Harmonics affect SQI/score; they do not directly decide valid.
 * - Final HR/RR are tracked and rate-limited outputs.
 * - RR transition evidence excludes continuity so true changes can leave the old track.
 * - RR local updates are deliberately smoother than confirmed distant transitions.
 */

#define VITAL_INPUT_FS_HZ                100U
#define VITAL_DECIMATION_FACTOR          4U
#define VITAL_FS_HZ                      (VITAL_INPUT_FS_HZ / VITAL_DECIMATION_FACTOR)

#define VITAL_HR_WINDOW_SEC              18U
#define VITAL_RR_WINDOW_SEC              10U
#define VITAL_WINDOW_SEC                 18U
#define VITAL_WINDOW_SAMPLES             (VITAL_FS_HZ * VITAL_WINDOW_SEC)
#define VITAL_HR_WINDOW_SAMPLES          (VITAL_FS_HZ * VITAL_HR_WINDOW_SEC)
#define VITAL_RR_WINDOW_SAMPLES          (VITAL_FS_HZ * VITAL_RR_WINDOW_SEC)

#define VITAL_PROFILE_HUMAN_TEST         1U

#if VITAL_PROFILE_HUMAN_TEST
#define VITAL_RR_MIN_BPM                 8.0f
#define VITAL_RR_MAX_BPM                 32.0f
#define VITAL_HR_MIN_BPM                 60.0f
#define VITAL_HR_MAX_BPM                 120.0f
#else
#define VITAL_RR_MIN_BPM                 6.0f
#define VITAL_RR_MAX_BPM                 90.0f
#define VITAL_HR_MIN_BPM                 80.0f
#define VITAL_HR_MAX_BPM                 260.0f
#endif

#define VITAL_RR_STEP_BPM                1.0f
#define VITAL_HR_STEP_BPM                1.0f
#define VITAL_Q_SCALE                    131072.0f
#define VITAL_HR_CANDIDATE_COUNT         3U
#define VITAL_ESTIMATE_PERIOD_SEC        1U

/* Final output thresholds. */
#define VITAL_RR_VALID_SQI_MIN            55U
#define VITAL_HR_VALID_SQI_MIN            65U
#define VITAL_HR_VALID_LOW_SQI_MIN        45U
#define VITAL_RR_PEAK_POWER_MIN           1.0e-10f

/* Tracker behaviour, expressed per one-second estimate. */
#define VITAL_HR_MAX_STEP_BPM_PER_EST     2.00f
#define VITAL_HR_TRANSITION_STEP_BPM       3.00f
#define VITAL_HR_RELEASE_STEP_BPM          4.00f
#define VITAL_RR_MAX_STEP_BPM_PER_EST     2.00f
#define VITAL_HR_SWITCH_DELTA_BPM         8.00f
#define VITAL_RR_SWITCH_DELTA_BPM         4.0f
#define VITAL_HR_PENDING_COUNT_REQUIRED   3U
#define VITAL_RR_PENDING_COUNT_REQUIRED   2U
#define VITAL_TRACK_INIT_COUNT_REQUIRED   2U
#define VITAL_MOTION_RECOVERY_HOLD_SEC     10U
#define VITAL_SWITCH_QUALITY_SCALE         1.00f
#define VITAL_TRANSITION_SQI_BLEND          0.85f
#define VITAL_TRANSITION_CONFIRM_SQI_MIN    62U
#define VITAL_REPORT_HOLD_SEC                4U

/* V18 multi-candidate HR evidence and contradicted-track release. */
#define VITAL_HR_EVIDENCE_MARGIN_CLEAR       0.06f
#define VITAL_HR_CONSENSUS_RADIUS_BPM       12.0f
#define VITAL_HR_CHALLENGER_MATCH_BPM        4.0f
#define VITAL_HR_CHALLENGER_DECAY            0.82f
#define VITAL_HR_CHALLENGER_CONFIRM          1.05f
#define VITAL_HR_CHALLENGER_MAX_GAP          1U
#define VITAL_HR_RELEASE_HITS_REQUIRED        2U
#define VITAL_HR_RELEASE_RECENT_MATCH_BPM     6.0f
#define VITAL_HR_RELEASE_RECENT_REJECT_BPM    8.0f
#define VITAL_HR_RELEASE_EVIDENCE_MARGIN      0.02f
#define VITAL_HR_RELEASE_CONVERGED_BPM        4.0f

typedef enum
{
    VITAL_AXIS_X = 0,
    VITAL_AXIS_Y = 1,
    VITAL_AXIS_Z = 2,
    VITAL_AXIS_NONE = 255
} VitalAxis_t;

typedef enum
{
    VITAL_HR_STATUS_NONE = 0,
    VITAL_HR_STATUS_VALID = 1,
    VITAL_HR_STATUS_CONTAM = 2,
    VITAL_HR_STATUS_VALID_LOW = 3,
    VITAL_HR_STATUS_TREND = 4
} VitalHrStatus_t;

typedef enum
{
    VITAL_MOTION_CLEAN = 0,
    VITAL_MOTION_MILD = 1,
    VITAL_MOTION_MODERATE = 2,
    VITAL_MOTION_STRONG = 3
} VitalMotionClass_t;

typedef struct
{
    float rr_bpm;
    float hr_bpm;

    uint8_t rr_valid;
    uint8_t rr_reportable;
    uint8_t rr_trend_active;

    uint8_t hr_valid;
    uint8_t hr_reportable;
    uint8_t hr_trend_active;
    VitalHrStatus_t hr_status;

    uint8_t rr_sqi;
    uint8_t hr_sqi;

    VitalAxis_t rr_axis;
    VitalAxis_t hr_axis;

    float rr_peak_freq_hz;
    float hr_peak_freq_hz;

    float rr_snr;
    float hr_snr;
    float hr_raw_snr;

    uint8_t window_fill_percent;
    uint16_t stored_samples;

    /* Kept for CSV compatibility. Hard x2,/2/history correction is removed. */
    uint8_t rr_corrected;
    uint8_t rr_energy_low;
    float rr_raw_bpm;
    float rr_alt_bpm;

    uint8_t hr_near_harmonic;
    uint8_t hr_harmonic_k;
    float hr_raw_bpm;
    float hr_clean_bpm;
    float hr_clean_snr;
    float hr_tracked_bpm;

    float hr_cand_bpm[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_snr[VITAL_HR_CANDIDATE_COUNT];
    uint8_t hr_cand_harm_k[VITAL_HR_CANDIDATE_COUNT];

    /* V18 HR evidence diagnostics.  These fields are intentionally exported
     * during PC validation so each SQI component can be checked against the
     * reference before the same logic is frozen for STM32. */
    uint8_t hr_cand_sqi[VITAL_HR_CANDIDATE_COUNT];
    uint8_t hr_cand_transition_sqi[VITAL_HR_CANDIDATE_COUNT];
    VitalAxis_t hr_cand_axis[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_prominence[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_axis_support[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_harmonic_quality[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_periodicity[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_half_consistency[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_recent_consistency[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_peak_shape[VITAL_HR_CANDIDATE_COUNT];
    float hr_cand_recent_bpm[VITAL_HR_CANDIDATE_COUNT];

    /* V14 quality diagnostics and regression fields. */
    float motion_quality;
    VitalMotionClass_t motion_class;

    float rr_candidate_bpm;
    float rr_prominence;
    float rr_axis_support;
    float rr_continuity_quality;
    float rr_harmonic_quality;

    float hr_candidate_bpm;
    float hr_prominence;
    float hr_axis_support;
    float hr_continuity_quality;
    float hr_harmonic_quality;

    /* V18 selected-path and SQI diagnostics. */
    float hr_candidate_margin;
    float hr_periodicity_quality;
    float hr_half_consistency;
    float hr_recent_consistency;
    float hr_peak_shape_quality;
    float hr_history_quality;
    float hr_axis_stability;
    float hr_output_agreement;
    float hr_rr_suspicion;
    float hr_challenger_bpm;
    float hr_challenger_evidence;
    uint8_t hr_consensus_used;
} VitalOutput_t;

typedef struct
{
    int16_t x[VITAL_WINDOW_SAMPLES];
    int16_t y[VITAL_WINDOW_SAMPLES];
    int16_t z[VITAL_WINDOW_SAMPLES];

    uint16_t write_index;
    uint16_t count;

    float gravity_x;
    float gravity_y;
    float gravity_z;
    float aa_x;
    float aa_y;
    float aa_z;
    uint8_t decim_count;
    uint8_t gravity_initialised;

    float rr_track_bpm;
    float rr_pending_bpm;
    uint8_t rr_pending_count;
    uint8_t rr_init_count;

    float hr_track_bpm;
    float hr_pending_bpm;
    uint8_t hr_pending_count;
    uint8_t hr_init_count;

    /* Primary spectral anchor preserves V15's conservative peak continuity.
     * V18's user-facing track can then fuse ambiguous nearby candidates
     * without feeding that fusion back into the next spectral ranking. */
    float hr_anchor_track_bpm;
    float hr_anchor_pending_bpm;
    uint8_t hr_anchor_pending_count;
    uint8_t hr_anchor_init_count;

    float hr_challenger_bpm;
    float hr_challenger_evidence;
    uint8_t hr_challenger_hits;
    uint8_t hr_challenger_primary_hits;
    uint8_t hr_challenger_primary_gap;
    uint8_t hr_challenger_gap;
    uint8_t hr_challenger_confirmed;
    uint8_t hr_challenger_release_hits;
    uint8_t hr_challenger_release_confirmed;

    float hr_track_support;
    VitalAxis_t hr_evidence_axis;
    uint8_t hr_evidence_axis_age;
    float hr_rr_suspicion;

    float motion_quality;
    VitalMotionClass_t motion_class;
    uint8_t motion_hold_seconds;

    uint8_t rr_report_hold_seconds;
    uint8_t hr_report_hold_seconds;

    VitalOutput_t last_output;
} VitalEstimator_t;

void VitalEstimator_Init(VitalEstimator_t *est);
void VitalEstimator_Reset(VitalEstimator_t *est);
void VitalEstimator_PushSample(VitalEstimator_t *est, const ImuSample_t *sample);
void VitalEstimator_SetMotionContext(VitalEstimator_t *est, const RestDetectorOutput_t *rest_out);
bool VitalEstimator_Estimate(VitalEstimator_t *est, VitalOutput_t *out);

const char *VitalEstimator_AxisToString(VitalAxis_t axis);
const char *VitalEstimator_HrStatusToString(VitalHrStatus_t status);
const char *VitalEstimator_MotionClassToString(VitalMotionClass_t motion_class);

#endif /* VITAL_ESTIMATOR_H */
