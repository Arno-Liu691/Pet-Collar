#ifndef REST_DETECTOR_H
#define REST_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * User configuration
 * ============================================================ */

#define REST_FS_HZ                       100U

#define REST_SHORT_WINDOW_S              2U
#define REST_SHORT_WINDOW_SAMPLES        (REST_FS_HZ * REST_SHORT_WINDOW_S)

/*
 * One local motion label is generated once per second.
 * Keep 30 seconds of label history.
 */
#define REST_HISTORY_SIZE                30U

#define REST_PENDING_WINDOW_S            10U
#define REST_MEASUREMENT_WINDOW_S        20U

/*
 * State machine thresholds.
 *
 * Interpretation:
 * - REST_PENDING means the collar motion is becoming stable.
 * - REST_MEASUREMENT means the collar is in a low-motion region where
 *   HR/RR signal processing may be attempted.
 *
 * REST_MEASUREMENT is NOT a final HR/RR output-valid decision.
 * A later signal-quality stage should decide whether a specific short
 * segment can actually update HR/RR.
 */
#define REST_ENTER_PENDING_CLEAN_RATIO           0.75f

/*
 * Normal entry path:
 * recent 20s is reasonably stable.
 *
 * This is deliberately slightly looser than the original 0.80 because
 * rest detection should not be responsible for proving HR/RR validity.
 */
#define REST_ENTER_MEASURE_CLEAN_RATIO_20S       0.75f

/*
 * Fast entry path:
 * even if the full 20s history is not yet clean, enter measurement mode
 * when the recent 10s is very clean.
 *
 * This allows locally stable middle sections to be used by later
 * signal-processing/SQI stages.
 */
#define REST_ENTER_MEASURE_CLEAN_RATIO_10S_FAST  0.85f

#define REST_EXIT_CLEAN_RATIO_10S                0.450f
#define REST_EXIT_CONSEC_DISTURB_COUNT           5U

/*
 * Backward-compatible name used by older PC test code.
 * It now refers to the normal 20s measurement-entry threshold.
 */
#define REST_ENTER_MEASURE_CLEAN_RATIO           REST_ENTER_MEASURE_CLEAN_RATIO_20S

/*
 * Local 2s clean-window thresholds.
 *
 * IMPORTANT:
 * These are starting values.
 * Ideally replace these with the short-window thresholds exported from
 * your Python preprocessing:
 *
 * hrrr_rest_reference_thresholds.csv
 *
 * They should correspond to:
 * - gyr_mag_p95_th
 * - gyr_mag_rms_th
 * - acc_mag_std_th
 * - acc_mag_range_th
 * - acc_jerk_p95_th
 */
#define REST_LOCAL_GYR_P95_TH            35.0f
#define REST_LOCAL_GYR_RMS_TH            20.0f
#define REST_LOCAL_ACC_STD_TH            0.030f
#define REST_LOCAL_ACC_RANGE_TH          0.35f
#define REST_LOCAL_ACC_JERK_P95_TH       3.0f

/*
 * Emergency strong-motion thresholds.
 * These are intentionally high. They are used to leave measurement mode
 * quickly when strong motion is detected.
 */
#define REST_STRONG_GYR_MAX_TH           250.0f
#define REST_STRONG_GYR_P95_TH           150.0f
#define REST_STRONG_ACC_RANGE_TH         2.0f
#define REST_STRONG_ACC_JERK_P95_TH      30.0f


/***************************************************************
 * Types
 ***************************************************************/

typedef struct
{
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
} ImuSample_t;


typedef enum
{
    REST_LOCAL_CLEAN = 0,
    REST_LOCAL_DISTURBANCE = 1
} RestLocalLabel_t;


typedef enum
{
    REST_STATE_NOT_REST = 0,
    REST_STATE_PENDING,
    REST_STATE_MEASUREMENT
} RestState_t;


typedef struct
{
    float acc_mag_mean;
    float acc_mag_std;
    float acc_mag_range;

    float gyr_mag_mean;
    float gyr_mag_rms;
    float gyr_mag_p95;
    float gyr_mag_max;

    float acc_jerk_rms;
    float acc_jerk_p95;
} RestShortFeatures_t;


typedef struct
{
    RestState_t state;
    RestLocalLabel_t local_label;

    float clean_ratio_10s;
    float clean_ratio_20s;

    uint8_t consecutive_disturbance_count;

    bool can_estimate_hrrr;
    bool strong_motion_detected;

    RestShortFeatures_t features_2s;
} RestDetectorOutput_t;


typedef struct
{
    ImuSample_t sample_ring[REST_SHORT_WINDOW_SAMPLES];
    uint16_t sample_write_index;
    uint16_t sample_count;

    RestLocalLabel_t label_history[REST_HISTORY_SIZE];
    uint8_t label_write_index;
    uint8_t label_count;

    RestState_t state;
    uint8_t consecutive_disturbance_count;

    bool last_update_valid;
    RestDetectorOutput_t last_output;
} RestDetector_t;


/***************************************************************
 * Public API
 ***************************************************************/

void RestDetector_Init(RestDetector_t *det);

bool RestDetector_PushSample(RestDetector_t *det, const ImuSample_t *sample);

/*
 * Call this once per second.
 *
 * On PC:
 * - call after every REST_FS_HZ samples (100 samples in this build).
 *
 * On STM32:
 * - call using a 1 Hz timer flag or sample counter.
 */
bool RestDetector_Update1s(RestDetector_t *det, RestDetectorOutput_t *out);

RestState_t RestDetector_GetState(const RestDetector_t *det);

const char *RestDetector_StateToString(RestState_t state);

const char *RestDetector_LocalLabelToString(RestLocalLabel_t label);

#endif /* REST_DETECTOR_H */
