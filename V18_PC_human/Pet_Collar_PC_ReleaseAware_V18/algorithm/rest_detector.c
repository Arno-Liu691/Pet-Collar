#include "rest_detector.h"

#include <string.h>
#include <math.h>

/***************************************************************
 * Internal utility functions
 ***************************************************************/

static float rest_squaref(float x)
{
    return x * x;
}


static float rest_acc_mag(const ImuSample_t *s)
{
    return sqrtf(
        rest_squaref(s->ax) +
        rest_squaref(s->ay) +
        rest_squaref(s->az)
    );
}


static float rest_gyr_mag(const ImuSample_t *s)
{
    return sqrtf(
        rest_squaref(s->gx) +
        rest_squaref(s->gy) +
        rest_squaref(s->gz)
    );
}


static void rest_sort_float(float *x, uint16_t n)
{
    uint16_t i;
    uint16_t j;

    for (i = 1U; i < n; i++)
    {
        float key = x[i];
        j = i;

        while ((j > 0U) && (x[j - 1U] > key))
        {
            x[j] = x[j - 1U];
            j--;
        }

        x[j] = key;
    }
}


static float rest_percentile_inplace(float *x, uint16_t n, float percentile)
{
    uint16_t index;

    if ((x == 0) || (n == 0U))
    {
        return 0.0f;
    }

    if (n > REST_SHORT_WINDOW_SAMPLES)
    {
        n = REST_SHORT_WINDOW_SAMPLES;
    }

    /*
     * The caller no longer needs the original order of this work array.
     * Sorting it in place removes the former additional 208-float temporary
     * array and lowers peak stack use without changing percentile results.
     */
    rest_sort_float(x, n);

    if (percentile <= 0.0f)
    {
        return x[0];
    }

    if (percentile >= 1.0f)
    {
        return x[n - 1U];
    }

    index = (uint16_t)((float)(n - 1U) * percentile);
    return x[index];
}


/***************************************************************
 * Internal sample-ring access
 ***************************************************************/

static bool rest_get_sample_chronological(
    const RestDetector_t *det,
    uint16_t chronological_index,
    ImuSample_t *sample_out
)
{
    uint16_t physical_index;

    if ((det == 0) || (sample_out == 0))
    {
        return false;
    }

    if (det->sample_count < REST_SHORT_WINDOW_SAMPLES)
    {
        return false;
    }

    if (chronological_index >= REST_SHORT_WINDOW_SAMPLES)
    {
        return false;
    }

    /*
     * sample_write_index points to the next write position.
     * When buffer is full, sample_write_index is the oldest sample.
     */
    physical_index = (uint16_t)(
        (det->sample_write_index + chronological_index) %
        REST_SHORT_WINDOW_SAMPLES
    );

    *sample_out = det->sample_ring[physical_index];
    return true;
}


/***************************************************************
 * 2s feature extraction
 ***************************************************************/

static bool rest_compute_2s_features(
    const RestDetector_t *det,
    RestShortFeatures_t *features
)
{
    float acc_mag[REST_SHORT_WINDOW_SAMPLES];
    float gyr_mag[REST_SHORT_WINDOW_SAMPLES];
    float acc_jerk_abs[REST_SHORT_WINDOW_SAMPLES];

    float acc_sum = 0.0f;
    float gyr_sum = 0.0f;
    float gyr_sq_sum = 0.0f;
    float acc_jerk_sq_sum = 0.0f;

    float acc_min;
    float acc_max;
    float gyr_max;

    if ((det == 0) || (features == 0))
    {
        return false;
    }

    if (det->sample_count < REST_SHORT_WINDOW_SAMPLES)
    {
        return false;
    }

    memset(features, 0, sizeof(RestShortFeatures_t));

    for (uint16_t i = 0U; i < REST_SHORT_WINDOW_SAMPLES; i++)
    {
        ImuSample_t s;

        if (!rest_get_sample_chronological(det, i, &s))
        {
            return false;
        }

        acc_mag[i] = rest_acc_mag(&s);
        gyr_mag[i] = rest_gyr_mag(&s);

        acc_sum += acc_mag[i];
        gyr_sum += gyr_mag[i];
        gyr_sq_sum += rest_squaref(gyr_mag[i]);

        if (i == 0U)
        {
            acc_jerk_abs[i] = 0.0f;
        }
        else
        {
            float diff = acc_mag[i] - acc_mag[i - 1U];
            float jerk = diff * (float)REST_FS_HZ;

            if (jerk < 0.0f)
            {
                jerk = -jerk;
            }

            acc_jerk_abs[i] = jerk;
            acc_jerk_sq_sum += rest_squaref(jerk);
        }
    }

    features->acc_mag_mean =
        acc_sum / (float)REST_SHORT_WINDOW_SAMPLES;

    features->gyr_mag_mean =
        gyr_sum / (float)REST_SHORT_WINDOW_SAMPLES;

    features->gyr_mag_rms =
        sqrtf(gyr_sq_sum / (float)REST_SHORT_WINDOW_SAMPLES);

    acc_min = acc_mag[0];
    acc_max = acc_mag[0];
    gyr_max = gyr_mag[0];

    for (uint16_t i = 1U; i < REST_SHORT_WINDOW_SAMPLES; i++)
    {
        if (acc_mag[i] < acc_min)
        {
            acc_min = acc_mag[i];
        }

        if (acc_mag[i] > acc_max)
        {
            acc_max = acc_mag[i];
        }

        if (gyr_mag[i] > gyr_max)
        {
            gyr_max = gyr_mag[i];
        }
    }

    features->acc_mag_range = acc_max - acc_min;
    features->gyr_mag_max = gyr_max;

    {
        float acc_var_sum = 0.0f;

        for (uint16_t i = 0U; i < REST_SHORT_WINDOW_SAMPLES; i++)
        {
            float d = acc_mag[i] - features->acc_mag_mean;
            acc_var_sum += d * d;
        }

        features->acc_mag_std =
            sqrtf(acc_var_sum / (float)REST_SHORT_WINDOW_SAMPLES);
    }

    features->gyr_mag_p95 =
        rest_percentile_inplace(gyr_mag, REST_SHORT_WINDOW_SAMPLES, 0.95f);

    features->acc_jerk_p95 =
        rest_percentile_inplace(acc_jerk_abs, REST_SHORT_WINDOW_SAMPLES, 0.95f);

    features->acc_jerk_rms =
        sqrtf(acc_jerk_sq_sum / (float)REST_SHORT_WINDOW_SAMPLES);

    return true;
}


/***************************************************************
 * Local clean / disturbance classification
 ***************************************************************/

static bool rest_is_strong_motion(const RestShortFeatures_t *f)
{
    if (f == 0)
    {
        return false;
    }

    if (f->gyr_mag_max >= REST_STRONG_GYR_MAX_TH)
    {
        return true;
    }

    if (f->gyr_mag_p95 >= REST_STRONG_GYR_P95_TH)
    {
        return true;
    }

    if (f->acc_mag_range >= REST_STRONG_ACC_RANGE_TH)
    {
        return true;
    }

    if (f->acc_jerk_p95 >= REST_STRONG_ACC_JERK_P95_TH)
    {
        return true;
    }

    return false;
}


static RestLocalLabel_t rest_classify_local_2s(
    const RestShortFeatures_t *f
)
{
    if (f == 0)
    {
        return REST_LOCAL_DISTURBANCE;
    }

    /*
     * Strict clean rule:
     * all local motion features must be below threshold.
     */
    if ((f->gyr_mag_p95 <= REST_LOCAL_GYR_P95_TH) &&
        (f->gyr_mag_rms <= REST_LOCAL_GYR_RMS_TH) &&
        (f->acc_mag_std <= REST_LOCAL_ACC_STD_TH) &&
        (f->acc_mag_range <= REST_LOCAL_ACC_RANGE_TH) &&
        (f->acc_jerk_p95 <= REST_LOCAL_ACC_JERK_P95_TH))
    {
        return REST_LOCAL_CLEAN;
    }

    return REST_LOCAL_DISTURBANCE;
}


/***************************************************************
 * Label history
 ***************************************************************/

static void rest_push_local_label(
    RestDetector_t *det,
    RestLocalLabel_t label
)
{
    if (det == 0)
    {
        return;
    }

    det->label_history[det->label_write_index] = label;

    det->label_write_index++;
    if (det->label_write_index >= REST_HISTORY_SIZE)
    {
        det->label_write_index = 0U;
    }

    if (det->label_count < REST_HISTORY_SIZE)
    {
        det->label_count++;
    }
}


static bool rest_get_recent_label(
    const RestDetector_t *det,
    uint8_t age_from_newest,
    RestLocalLabel_t *label_out
)
{
    int16_t index;

    if ((det == 0) || (label_out == 0))
    {
        return false;
    }

    if (age_from_newest >= det->label_count)
    {
        return false;
    }

    /*
     * label_write_index points to next write location.
     * newest label is one position before it.
     */
    index = (int16_t)det->label_write_index - 1 - (int16_t)age_from_newest;

    while (index < 0)
    {
        index += REST_HISTORY_SIZE;
    }

    *label_out = det->label_history[(uint8_t)index];
    return true;
}


static float rest_clean_ratio_recent(
    const RestDetector_t *det,
    uint8_t seconds
)
{
    uint8_t n;
    uint8_t clean_count = 0U;

    if (det == 0)
    {
        return 0.0f;
    }

    n = seconds;

    if (n > det->label_count)
    {
        n = det->label_count;
    }

    if (n == 0U)
    {
        return 0.0f;
    }

    for (uint8_t i = 0U; i < n; i++)
    {
        RestLocalLabel_t label;

        if (rest_get_recent_label(det, i, &label))
        {
            if (label == REST_LOCAL_CLEAN)
            {
                clean_count++;
            }
        }
    }

    return (float)clean_count / (float)n;
}


/***************************************************************
 * State machine
 ***************************************************************/

static void rest_update_state_machine(
    RestDetector_t *det,
    RestLocalLabel_t local_label,
    bool strong_motion,
    float clean_ratio_10s,
    float clean_ratio_20s
)
{
    if (det == 0)
    {
        return;
    }

    if ((local_label == REST_LOCAL_DISTURBANCE) || strong_motion)
    {
        if (det->consecutive_disturbance_count < 255U)
        {
            det->consecutive_disturbance_count++;
        }
    }
    else
    {
        det->consecutive_disturbance_count = 0U;
    }

    switch (det->state)
    {
        case REST_STATE_NOT_REST:
        {
            if ((det->label_count >= REST_PENDING_WINDOW_S) &&
                (clean_ratio_10s >= REST_ENTER_PENDING_CLEAN_RATIO) &&
                (!strong_motion))
            {
                det->state = REST_STATE_PENDING;
            }
            break;
        }

        case REST_STATE_PENDING:
        {
            if (strong_motion ||
                (det->consecutive_disturbance_count >= REST_EXIT_CONSEC_DISTURB_COUNT) ||
                ((det->label_count >= REST_PENDING_WINDOW_S) &&
                 (clean_ratio_10s < REST_EXIT_CLEAN_RATIO_10S)))
            {
                det->state = REST_STATE_NOT_REST;
            }
            else
            {
                bool normal_20s_entry;
                bool fast_10s_entry;

                /*
                 * Normal path:
                 * the recent 20s history is reasonably stable.
                 */
                normal_20s_entry =
                    (det->label_count >= REST_MEASUREMENT_WINDOW_S) &&
                    (clean_ratio_20s >= REST_ENTER_MEASURE_CLEAN_RATIO_20S);

                /*
                 * Fast path:
                 * the full 20s history may include earlier motion, but the
                 * recent 10s is very clean. This allows locally stable
                 * sections to enter measurement mode earlier.
                 */
                fast_10s_entry =
                    (det->label_count >= REST_PENDING_WINDOW_S) &&
                    (clean_ratio_10s >= REST_ENTER_MEASURE_CLEAN_RATIO_10S_FAST);

                if ((normal_20s_entry || fast_10s_entry) && (!strong_motion))
                {
                    det->state = REST_STATE_MEASUREMENT;
                }
            }
            break;
        }

        case REST_STATE_MEASUREMENT:
        {
            /*
             * Exit is intentionally more tolerant than entry.
             * Do not exit for a single spike.
             */
            if (strong_motion ||
                (det->consecutive_disturbance_count >= REST_EXIT_CONSEC_DISTURB_COUNT) ||
                ((det->label_count >= REST_PENDING_WINDOW_S) &&
                 (clean_ratio_10s < REST_EXIT_CLEAN_RATIO_10S)))
            {
                det->state = REST_STATE_NOT_REST;
            }
            break;
        }

        default:
        {
            det->state = REST_STATE_NOT_REST;
            break;
        }
    }
}


/***************************************************************
 * Public API implementation
 ***************************************************************/

void RestDetector_Init(RestDetector_t *det)
{
    if (det == 0)
    {
        return;
    }

    memset(det, 0, sizeof(RestDetector_t));

    det->state = REST_STATE_NOT_REST;
    det->sample_write_index = 0U;
    det->sample_count = 0U;

    det->label_write_index = 0U;
    det->label_count = 0U;

    det->consecutive_disturbance_count = 0U;
    det->last_update_valid = false;
}


bool RestDetector_PushSample(RestDetector_t *det, const ImuSample_t *sample)
{
    if ((det == 0) || (sample == 0))
    {
        return false;
    }

    det->sample_ring[det->sample_write_index] = *sample;

    det->sample_write_index++;
    if (det->sample_write_index >= REST_SHORT_WINDOW_SAMPLES)
    {
        det->sample_write_index = 0U;
    }

    if (det->sample_count < REST_SHORT_WINDOW_SAMPLES)
    {
        det->sample_count++;
    }

    return true;
}


bool RestDetector_Update1s(RestDetector_t *det, RestDetectorOutput_t *out)
{
    RestShortFeatures_t features;
    RestLocalLabel_t local_label;
    bool strong_motion;
    float clean_ratio_10s;
    float clean_ratio_20s;

    if ((det == 0) || (out == 0))
    {
        return false;
    }

    memset(out, 0, sizeof(RestDetectorOutput_t));

    if (det->sample_count < REST_SHORT_WINDOW_SAMPLES)
    {
        out->state = det->state;
        out->local_label = REST_LOCAL_DISTURBANCE;
        out->can_estimate_hrrr = false;
        det->last_output = *out;
        det->last_update_valid = false;
        return false;
    }

    if (!rest_compute_2s_features(det, &features))
    {
        return false;
    }

    local_label = rest_classify_local_2s(&features);
    strong_motion = rest_is_strong_motion(&features);

    rest_push_local_label(det, local_label);

    clean_ratio_10s = rest_clean_ratio_recent(det, REST_PENDING_WINDOW_S);
    clean_ratio_20s = rest_clean_ratio_recent(det, REST_MEASUREMENT_WINDOW_S);

    rest_update_state_machine(
        det,
        local_label,
        strong_motion,
        clean_ratio_10s,
        clean_ratio_20s
    );

    out->state = det->state;
    out->local_label = local_label;
    out->clean_ratio_10s = clean_ratio_10s;
    out->clean_ratio_20s = clean_ratio_20s;
    out->consecutive_disturbance_count = det->consecutive_disturbance_count;
    out->can_estimate_hrrr = (det->state == REST_STATE_MEASUREMENT);
    out->strong_motion_detected = strong_motion;
    out->features_2s = features;

    det->last_output = *out;
    det->last_update_valid = true;

    return true;
}


RestState_t RestDetector_GetState(const RestDetector_t *det)
{
    if (det == 0)
    {
        return REST_STATE_NOT_REST;
    }

    return det->state;
}


const char *RestDetector_StateToString(RestState_t state)
{
    switch (state)
    {
        case REST_STATE_NOT_REST:
            return "NOT_REST";

        case REST_STATE_PENDING:
            return "REST_PENDING";

        case REST_STATE_MEASUREMENT:
            return "REST_MEASUREMENT";

        default:
            return "UNKNOWN";
    }
}


const char *RestDetector_LocalLabelToString(RestLocalLabel_t label)
{
    switch (label)
    {
        case REST_LOCAL_CLEAN:
            return "CLEAN_LOCAL";

        case REST_LOCAL_DISTURBANCE:
            return "DISTURBANCE";

        default:
            return "UNKNOWN";
    }
}