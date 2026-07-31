#include "vital_estimator.h"

/* Pet Collar PC Release-Aware V18. */
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define VITAL_TWO_PI          (2.0f * (float)M_PI)
#define VITAL_EPS             1.0e-12f
#define VITAL_GRAVITY_ALPHA   0.003328f
#define VITAL_AA_ALPHA        0.34065f
#define VITAL_MAX_BINS        64U
#define VITAL_INTERNAL_CANDS  6U

typedef struct
{
    float bpm;
    float freq_hz;
    float snr;
    float peak_power;
    float noise_power;
    float prominence;
    float axis_support;
    float continuity_quality;
    float harmonic_quality;
    float transition_quality;
    float periodicity_quality;
    float half_consistency;
    float recent_consistency;
    float peak_shape_quality;
    float recent_bpm;
    float structural_quality;
    float evidence_quality;
    float score;
    uint8_t sqi;
    uint8_t transition_sqi;
    VitalAxis_t axis;
    uint8_t near_harmonic;
    uint8_t harmonic_k;
} VitalCandidate_t;

typedef struct
{
    VitalCandidate_t top[VITAL_INTERNAL_CANDS];
    VitalCandidate_t raw_best;
    VitalCandidate_t clean_best;
    uint8_t count;
} VitalSearchResult_t;

typedef struct
{
    VitalCandidate_t evidence_candidate;
    float target_bpm;
    float candidate_margin;
    uint8_t consensus_used;
    uint8_t transition_active;
    uint8_t release_active;
} VitalHrSelection_t;

static float Vital_AbsF(float x) { return (x >= 0.0f) ? x : -x; }

static float Vital_ClampF(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int16_t Vital_ClampI16FromFloat(float x)
{
    if (x > 32767.0f) return 32767;
    if (x < -32768.0f) return -32768;
    return (int16_t)x;
}

static uint8_t Vital_QualityToSqi(float quality)
{
    quality = Vital_ClampF(quality, 0.0f, 1.0f);
    return (uint8_t)(quality * 100.0f + 0.5f);
}

static const int16_t *Vital_GetAxisBuffer(const VitalEstimator_t *est, VitalAxis_t axis)
{
    switch (axis)
    {
        case VITAL_AXIS_X: return est->x;
        case VITAL_AXIS_Y: return est->y;
        case VITAL_AXIS_Z: return est->z;
        default: return est->x;
    }
}

static int16_t Vital_GetWindowSample(const VitalEstimator_t *est,
                                     VitalAxis_t axis,
                                     uint16_t window_samples,
                                     uint16_t n)
{
    const int16_t *buf = Vital_GetAxisBuffer(est, axis);
    uint16_t start;
    uint16_t idx;

    if (est->count < window_samples) return 0;
    start = (uint16_t)((est->write_index + VITAL_WINDOW_SAMPLES - window_samples) % VITAL_WINDOW_SAMPLES);
    idx = (uint16_t)((start + n) % VITAL_WINDOW_SAMPLES);
    return buf[idx];
}

static int16_t Vital_GetSegmentSample(const VitalEstimator_t *est,
                                      VitalAxis_t axis,
                                      uint16_t full_window_samples,
                                      uint16_t segment_offset,
                                      uint16_t n)
{
    const int16_t *buf = Vital_GetAxisBuffer(est, axis);
    uint16_t start;
    uint16_t idx;

    if (est->count < full_window_samples) return 0;
    start = (uint16_t)((est->write_index + VITAL_WINDOW_SAMPLES - full_window_samples) %
                       VITAL_WINDOW_SAMPLES);
    idx = (uint16_t)((start + segment_offset + n) % VITAL_WINDOW_SAMPLES);
    return buf[idx];
}

static float Vital_ComputeSegmentMean(const VitalEstimator_t *est,
                                      VitalAxis_t axis,
                                      uint16_t full_window_samples,
                                      uint16_t segment_offset,
                                      uint16_t segment_samples)
{
    uint16_t i;
    float sum = 0.0f;

    for (i = 0U; i < segment_samples; i++)
    {
        sum += ((float)Vital_GetSegmentSample(est,
                                              axis,
                                              full_window_samples,
                                              segment_offset,
                                              i)) / VITAL_Q_SCALE;
    }
    return sum / (float)segment_samples;
}

static float Vital_GoertzelPowerSegment(const VitalEstimator_t *est,
                                        VitalAxis_t axis,
                                        uint16_t full_window_samples,
                                        uint16_t segment_offset,
                                        uint16_t segment_samples,
                                        float mean,
                                        float freq_hz,
                                        uint8_t diff_order)
{
    uint16_t i;
    float omega = VITAL_TWO_PI * freq_hz / (float)VITAL_FS_HZ;
    float coeff = 2.0f * cosf(omega);
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    float prev1 = 0.0f;
    float prev2 = 0.0f;

    for (i = 0U; i < segment_samples; i++)
    {
        float x = ((float)Vital_GetSegmentSample(est,
                                                 axis,
                                                 full_window_samples,
                                                 segment_offset,
                                                 i)) / VITAL_Q_SCALE;
        x -= mean;

        if (diff_order == 1U)
        {
            float d = (i == 0U) ? 0.0f : (x - prev1);
            prev1 = x;
            x = d;
        }
        else if (diff_order >= 2U)
        {
            float d2 = (i < 2U) ? 0.0f : (x - 2.0f * prev1 + prev2);
            prev2 = prev1;
            prev1 = x;
            x = d2;
        }

        q0 = coeff * q1 - q2 + x;
        q2 = q1;
        q1 = q0;
    }

    {
        float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
        power /= ((float)segment_samples * (float)segment_samples + VITAL_EPS);
        return (power > 0.0f) ? power : 0.0f;
    }
}

static float Vital_ComputeMean(const VitalEstimator_t *est,
                               VitalAxis_t axis,
                               uint16_t window_samples)
{
    uint16_t i;
    float sum = 0.0f;
    for (i = 0U; i < window_samples; i++)
    {
        sum += ((float)Vital_GetWindowSample(est, axis, window_samples, i)) / VITAL_Q_SCALE;
    }
    return sum / (float)window_samples;
}

static float Vital_GoertzelPowerWithMean(const VitalEstimator_t *est,
                                         VitalAxis_t axis,
                                         uint16_t window_samples,
                                         float mean,
                                         float freq_hz,
                                         uint8_t diff_order)
{
    uint16_t i;
    float omega = VITAL_TWO_PI * freq_hz / (float)VITAL_FS_HZ;
    float coeff = 2.0f * cosf(omega);
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    float prev1 = 0.0f;
    float prev2 = 0.0f;

    for (i = 0U; i < window_samples; i++)
    {
        float x = ((float)Vital_GetWindowSample(est, axis, window_samples, i)) / VITAL_Q_SCALE;
        x -= mean;

        if (diff_order == 1U)
        {
            float d = (i == 0U) ? 0.0f : (x - prev1);
            prev1 = x;
            x = d;
        }
        else if (diff_order >= 2U)
        {
            float d2 = (i < 2U) ? 0.0f : (x - 2.0f * prev1 + prev2);
            prev2 = prev1;
            prev1 = x;
            x = d2;
        }

        q0 = coeff * q1 - q2 + x;
        q2 = q1;
        q1 = q0;
    }

    {
        float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
        power /= ((float)window_samples * (float)window_samples + VITAL_EPS);
        return (power > 0.0f) ? power : 0.0f;
    }
}

static float Vital_AutocorrelationAtLag(const VitalEstimator_t *est,
                                        VitalAxis_t axis,
                                        uint16_t window_samples,
                                        uint16_t lag)
{
    uint16_t i;
    uint16_t pairs;
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float sum_x2 = 0.0f;
    float sum_y2 = 0.0f;
    float sum_xy = 0.0f;

    if ((lag < 1U) || ((uint32_t)lag * 2U >= window_samples)) return 0.0f;
    pairs = (uint16_t)(window_samples - lag);

    /* Correlate the detrended acceleration itself.  The first-difference
     * spectrum is excellent for finding small mechanical peaks, but its
     * sample-to-sample noise made period-domain evidence unnecessarily weak. */
    for (i = 0U; i < pairs; i++)
    {
        float x = ((float)Vital_GetWindowSample(est, axis, window_samples, i)) /
                  VITAL_Q_SCALE;
        float y = ((float)Vital_GetWindowSample(est, axis, window_samples,
                                                 (uint16_t)(i + lag))) /
                  VITAL_Q_SCALE;
        sum_x += x;
        sum_y += y;
        sum_x2 += x * x;
        sum_y2 += y * y;
        sum_xy += x * y;
    }

    {
        float n = (float)pairs;
        float covariance = sum_xy - (sum_x * sum_y / n);
        float energy_x = sum_x2 - (sum_x * sum_x / n);
        float energy_y = sum_y2 - (sum_y * sum_y / n);
        float denom = sqrtf(Vital_ClampF(energy_x * energy_y, 0.0f, 1.0e30f));
        if (denom <= VITAL_EPS) return 0.0f;
        return Vital_ClampF(covariance / denom, -1.0f, 1.0f);
    }
}

static float Vital_HrPeriodicityQuality(const VitalEstimator_t *est,
                                        VitalAxis_t axis,
                                        float bpm)
{
    int16_t delta;
    uint16_t nominal_lag;
    float best = 0.0f;

    if (bpm < 1.0f) return 0.0f;
    nominal_lag = (uint16_t)((float)VITAL_FS_HZ * 60.0f / bpm + 0.5f);

    for (delta = -1; delta <= 1; delta++)
    {
        int16_t lag_i = (int16_t)nominal_lag + delta;
        float corr1;
        float corr2;
        float combined;
        if (lag_i < 2) continue;
        corr1 = Vital_AutocorrelationAtLag(est,
                                            axis,
                                            VITAL_HR_WINDOW_SAMPLES,
                                            (uint16_t)lag_i);
        corr2 = Vital_AutocorrelationAtLag(est,
                                            axis,
                                            VITAL_HR_WINDOW_SAMPLES,
                                            (uint16_t)(2 * lag_i));
        combined = 0.70f * Vital_ClampF(corr1, 0.0f, 1.0f) +
                   0.30f * Vital_ClampF(corr2, 0.0f, 1.0f);
        if (combined > best) best = combined;
    }

    return Vital_ClampF((best - 0.05f) / 0.70f, 0.0f, 1.0f);
}

static float Vital_FindLocalBestBpm(const VitalEstimator_t *est,
                                    VitalAxis_t axis,
                                    uint16_t full_window_samples,
                                    uint16_t segment_offset,
                                    uint16_t segment_samples,
                                    float bpm_min,
                                    float bpm_max,
                                    float bpm_step,
                                    float *best_power)
{
    float mean;
    float bpm;
    float selected_bpm = bpm_min;
    float selected_power = -1.0f;

    bpm_min = Vital_ClampF(bpm_min, VITAL_HR_MIN_BPM, VITAL_HR_MAX_BPM);
    bpm_max = Vital_ClampF(bpm_max, VITAL_HR_MIN_BPM, VITAL_HR_MAX_BPM);
    mean = Vital_ComputeSegmentMean(est,
                                    axis,
                                    full_window_samples,
                                    segment_offset,
                                    segment_samples);

    for (bpm = bpm_min; bpm <= bpm_max + 0.01f; bpm += bpm_step)
    {
        float power = Vital_GoertzelPowerSegment(est,
                                                 axis,
                                                 full_window_samples,
                                                 segment_offset,
                                                 segment_samples,
                                                 mean,
                                                 bpm / 60.0f,
                                                 1U);
        if (power > selected_power)
        {
            selected_power = power;
            selected_bpm = bpm;
        }
    }

    if (best_power != 0) *best_power = (selected_power > 0.0f) ? selected_power : 0.0f;
    return selected_bpm;
}

static void Vital_EnrichHrCandidate(const VitalEstimator_t *est,
                                    VitalCandidate_t *cand)
{
    const uint16_t half_samples = VITAL_HR_WINDOW_SAMPLES / 2U;
    const uint16_t recent_samples = 8U * VITAL_FS_HZ;
    float front_power = 0.0f;
    float back_power = 0.0f;
    float front_global_power = 0.0f;
    float back_global_power = 0.0f;
    float recent_power = 0.0f;
    float recent_global_power = 0.0f;
    float front_bpm;
    float back_bpm;
    float front_global_bpm;
    float back_global_bpm;
    float recent_bpm;
    float recent_global_bpm;
    float frequency_agreement;
    float centre_agreement;
    float amplitude_agreement;
    float split_presence;
    float recent_presence;

    cand->periodicity_quality = Vital_HrPeriodicityQuality(est, cand->axis, cand->bpm);

    front_bpm = Vital_FindLocalBestBpm(est,
                                       cand->axis,
                                       VITAL_HR_WINDOW_SAMPLES,
                                       0U,
                                       half_samples,
                                       cand->bpm - 6.0f,
                                       cand->bpm + 6.0f,
                                       2.0f,
                                       &front_power);
    back_bpm = Vital_FindLocalBestBpm(est,
                                      cand->axis,
                                      VITAL_HR_WINDOW_SAMPLES,
                                      half_samples,
                                      half_samples,
                                      cand->bpm - 6.0f,
                                      cand->bpm + 6.0f,
                                      2.0f,
                                      &back_power);
    front_global_bpm = Vital_FindLocalBestBpm(est,
                                              cand->axis,
                                              VITAL_HR_WINDOW_SAMPLES,
                                              0U,
                                              half_samples,
                                              VITAL_HR_MIN_BPM,
                                              VITAL_HR_MAX_BPM,
                                              2.0f,
                                              &front_global_power);
    back_global_bpm = Vital_FindLocalBestBpm(est,
                                             cand->axis,
                                             VITAL_HR_WINDOW_SAMPLES,
                                             half_samples,
                                             half_samples,
                                             VITAL_HR_MIN_BPM,
                                             VITAL_HR_MAX_BPM,
                                             2.0f,
                                             &back_global_power);
    (void)front_global_bpm;
    (void)back_global_bpm;
    frequency_agreement = Vital_ClampF(1.0f - Vital_AbsF(front_bpm - back_bpm) / 14.0f,
                                       0.0f,
                                       1.0f);
    centre_agreement = Vital_ClampF(
        1.0f - Vital_AbsF(0.5f * (front_bpm + back_bpm) - cand->bpm) / 10.0f,
        0.0f,
        1.0f);
    amplitude_agreement = sqrtf(
        (Vital_ClampF((front_power < back_power) ? front_power : back_power, 0.0f, 1.0e30f) +
         VITAL_EPS) /
        (((front_power > back_power) ? front_power : back_power) + VITAL_EPS));
    split_presence = sqrtf(Vital_ClampF(front_power / (front_global_power + VITAL_EPS),
                                        0.0f,
                                        1.0f) *
                              Vital_ClampF(back_power / (back_global_power + VITAL_EPS),
                                           0.0f,
                                           1.0f));
    cand->half_consistency = 0.38f * frequency_agreement +
                             0.18f * centre_agreement +
                             0.18f * amplitude_agreement +
                             0.26f * split_presence;

    recent_bpm = Vital_FindLocalBestBpm(est,
                                        cand->axis,
                                        recent_samples,
                                        0U,
                                        recent_samples,
                                        cand->bpm - 6.0f,
                                        cand->bpm + 6.0f,
                                        2.0f,
                                        &recent_power);
    recent_global_bpm = Vital_FindLocalBestBpm(est,
                                               cand->axis,
                                               recent_samples,
                                               0U,
                                               recent_samples,
                                               VITAL_HR_MIN_BPM,
                                               VITAL_HR_MAX_BPM,
                                               2.0f,
                                               &recent_global_power);
    recent_presence = sqrtf(Vital_ClampF(recent_power / (recent_global_power + VITAL_EPS),
                                         0.0f,
                                         1.0f));
    cand->recent_bpm = recent_global_bpm;
    /* A candidate-local peak alone is not enough: a broad/high-SNR remnant of
     * the old path can remain locally visible after the recent window has
     * moved elsewhere.  Give the recent window's global peak enough weight to
     * contradict that stale path. */
    cand->recent_consistency =
        0.35f * recent_presence +
        0.15f * Vital_ClampF(1.0f - Vital_AbsF(recent_bpm - cand->bpm) / 10.0f,
                             0.0f,
                             1.0f) +
        0.50f * Vital_ClampF(1.0f - Vital_AbsF(recent_global_bpm - cand->bpm) / 18.0f,
                             0.0f,
                             1.0f);

    cand->structural_quality =
        0.22f * cand->half_consistency +
        0.30f * cand->recent_consistency +
        0.18f * cand->peak_shape_quality +
        0.12f * cand->axis_support +
        0.18f * cand->periodicity_quality;
    cand->evidence_quality =
        0.64f * cand->transition_quality +
        0.36f * cand->structural_quality;
}

static float Vital_SnrQuality(float snr, uint8_t is_hr)
{
    if (is_hr != 0U)
    {
        return Vital_ClampF((snr - 2.2f) / 6.8f, 0.0f, 1.0f);
    }
    return Vital_ClampF((snr - 1.8f) / 7.0f, 0.0f, 1.0f);
}

static float Vital_ProminenceQuality(float prominence)
{
    return Vital_ClampF((prominence - 1.0f) / 1.20f, 0.0f, 1.0f);
}

static float Vital_HrContinuityQuality(float bpm, float track)
{
    float d;
    if (track < 1.0f) return 0.70f;
    d = Vital_AbsF(bpm - track);
    if (d <= 3.0f) return 1.00f;
    if (d <= 8.0f) return 0.85f;
    if (d <= 15.0f) return 0.50f;
    if (d <= 25.0f) return 0.20f;
    return 0.05f;
}

static float Vital_RrContinuityQuality(float bpm, float track)
{
    float d;
    if (track < 1.0f) return 0.70f;
    d = Vital_AbsF(bpm - track);
    if (d <= 1.5f) return 1.00f;
    if (d <= 3.0f) return 0.82f;
    if (d <= 6.0f) return 0.48f;
    if (d <= 10.0f) return 0.22f;
    return 0.08f;
}

static uint8_t Vital_FindRrHarmonic(float bpm, float rr_bpm, uint8_t *out_k)
{
    uint8_t k;
    if (out_k != 0) *out_k = 0U;
    if (rr_bpm < 1.0f) return 0U;

    for (k = 2U; k <= 12U; k++)
    {
        if (Vital_AbsF(bpm - rr_bpm * (float)k) <= 2.5f)
        {
            if (out_k != 0) *out_k = k;
            return 1U;
        }
    }
    return 0U;
}

static float Vital_HrHarmonicQuality(float bpm,
                                     VitalAxis_t axis,
                                     float rr_bpm,
                                     uint8_t rr_sqi,
                                     VitalAxis_t rr_axis,
                                     uint8_t *near_harm,
                                     uint8_t *harmonic_k)
{
    float q = 1.0f;
    uint8_t k = 0U;
    uint8_t near = Vital_FindRrHarmonic(bpm, rr_bpm, &k);

    if (near != 0U)
    {
        if (k <= 2U) q = 0.20f;
        else if (k == 3U) q = 0.25f;
        else if (k == 4U) q = 0.45f;
        else if (k == 5U) q = 0.72f;
        else if (k == 6U) q = 0.82f;
        else q = 0.96f;

        /* Coupling evidence is stronger when RR is confident and on the same axis. */
        if (rr_sqi < 70U) q = 0.55f + 0.45f * q;
        if ((rr_axis == VITAL_AXIS_NONE) || (axis != rr_axis)) q = 0.60f + 0.40f * q;
    }

    if (near_harm != 0) *near_harm = near;
    if (harmonic_k != 0) *harmonic_k = k;
    return q;
}

static void Vital_InsertCandidate(VitalSearchResult_t *result, const VitalCandidate_t *candidate)
{
    uint8_t i;
    uint8_t j;

    if ((result == 0) || (candidate == 0)) return;

    /* Keep frequency-distinct peaks. If a close peak already exists, retain the better one. */
    for (i = 0U; i < result->count; i++)
    {
        if (Vital_AbsF(result->top[i].bpm - candidate->bpm) < 2.0f)
        {
            if (candidate->score <= result->top[i].score) return;
            for (j = i; (j + 1U) < result->count; j++) result->top[j] = result->top[j + 1U];
            result->count--;
            break;
        }
    }

    if (result->count < VITAL_INTERNAL_CANDS) result->count++;

    for (i = 0U; i < result->count; i++)
    {
        if (candidate->score > result->top[i].score)
        {
            for (j = (uint8_t)(result->count - 1U); j > i; j--) result->top[j] = result->top[j - 1U];
            result->top[i] = *candidate;
            return;
        }
    }

    if (result->count > 0U) result->top[result->count - 1U] = *candidate;
}

static float Vital_NoiseExcludingPeak(const float *p, uint8_t bins, uint8_t index)
{
    uint8_t i;
    uint8_t count = 0U;
    float sum = 0.0f;

    for (i = 0U; i < bins; i++)
    {
        int16_t d = (int16_t)i - (int16_t)index;
        if ((d >= -2) && (d <= 2)) continue;
        sum += p[i];
        count++;
    }

    if (count == 0U) return VITAL_EPS;
    sum /= (float)count;
    return (sum > VITAL_EPS) ? sum : VITAL_EPS;
}

static VitalSearchResult_t Vital_SearchBand(const VitalEstimator_t *est,
                                            uint16_t window_samples,
                                            float bpm_min,
                                            float bpm_max,
                                            float bpm_step,
                                            uint8_t diff_order,
                                            uint8_t is_hr,
                                            float track_bpm,
                                            float rr_bpm,
                                            uint8_t rr_sqi,
                                            VitalAxis_t rr_axis)
{
    VitalSearchResult_t result;
    float power[3][VITAL_MAX_BINS];
    float means[3];
    uint8_t bins = 0U;
    uint8_t a;
    uint8_t i;
    float bpm;

    memset(&result, 0, sizeof(result));
    result.raw_best.axis = VITAL_AXIS_NONE;
    result.clean_best.axis = VITAL_AXIS_NONE;

    for (bpm = bpm_min; bpm <= bpm_max + 0.01f; bpm += bpm_step)
    {
        if (bins < VITAL_MAX_BINS) bins++;
    }

    means[0] = Vital_ComputeMean(est, VITAL_AXIS_X, window_samples);
    means[1] = Vital_ComputeMean(est, VITAL_AXIS_Y, window_samples);
    means[2] = Vital_ComputeMean(est, VITAL_AXIS_Z, window_samples);

    for (a = 0U; a < 3U; a++)
    {
        VitalAxis_t axis = (VitalAxis_t)a;
        for (i = 0U; i < bins; i++)
        {
            float this_bpm = bpm_min + (float)i * bpm_step;
            power[a][i] = Vital_GoertzelPowerWithMean(est,
                                                      axis,
                                                      window_samples,
                                                      means[a],
                                                      this_bpm / 60.0f,
                                                      diff_order);
        }
    }

    for (i = 0U; i < bins; i++)
    {
        float snr_axis[3];
        float best_snr;
        float second_snr;
        uint8_t best_a = 0U;
        float p;
        float left;
        float right;
        float prominence;
        float support;
        float continuity;
        float harmonic_quality = 1.0f;
        float snr_quality;
        float prom_quality;
        float peak_shape_quality = 0.0f;
        float score;
        float quality;
        float transition_quality;
        uint8_t near_harm = 0U;
        uint8_t harmonic_k = 0U;
        VitalCandidate_t cand;
        uint8_t is_local_peak = 1U;

        for (a = 0U; a < 3U; a++)
        {
            float noise = Vital_NoiseExcludingPeak(power[a], bins, i);
            snr_axis[a] = power[a][i] / noise;
        }

        best_snr = snr_axis[0];
        if (snr_axis[1] > best_snr) { best_snr = snr_axis[1]; best_a = 1U; }
        if (snr_axis[2] > best_snr) { best_snr = snr_axis[2]; best_a = 2U; }

        second_snr = 0.0f;
        for (a = 0U; a < 3U; a++)
        {
            if ((a != best_a) && (snr_axis[a] > second_snr)) second_snr = snr_axis[a];
        }

        p = power[best_a][i];
        left = (i > 0U) ? power[best_a][i - 1U] : p;
        right = ((i + 1U) < bins) ? power[best_a][i + 1U] : p;
        prominence = p / (0.5f * (left + right) + VITAL_EPS);
        support = Vital_ClampF(second_snr / (best_snr + VITAL_EPS), 0.0f, 1.0f);

        if (is_hr != 0U)
        {
            uint8_t j;
            uint8_t background_count = 0U;
            uint8_t half_power_width = 0U;
            float local_background = 0.0f;
            float contrast_quality;
            float width_quality;

            for (j = 0U; j < bins; j++)
            {
                int16_t distance = (int16_t)j - (int16_t)i;
                int16_t abs_distance = (distance >= 0) ? distance : (int16_t)-distance;
                if ((abs_distance <= 8) && (power[best_a][j] >= 0.5f * p))
                {
                    half_power_width++;
                }
                if ((abs_distance >= 5) && (abs_distance <= 10))
                {
                    local_background += power[best_a][j];
                    background_count++;
                }
            }

            if (background_count > 0U) local_background /= (float)background_count;
            contrast_quality = Vital_ClampF(
                (p / (local_background + VITAL_EPS) - 1.0f) / 3.0f,
                0.0f,
                1.0f);
            if (half_power_width <= 6U) width_quality = 1.0f;
            else if (half_power_width <= 8U) width_quality = 0.75f;
            else if (half_power_width <= 10U) width_quality = 0.45f;
            else width_quality = 0.20f;
            peak_shape_quality = 0.70f * contrast_quality + 0.30f * width_quality;
        }

        /* Local maxima only, while retaining edges for raw diagnostics. */
        if ((i > 0U) && (p < power[best_a][i - 1U])) is_local_peak = 0U;
        if (((i + 1U) < bins) && (p < power[best_a][i + 1U])) is_local_peak = 0U;

        bpm = bpm_min + (float)i * bpm_step;
        continuity = (is_hr != 0U) ? Vital_HrContinuityQuality(bpm, track_bpm)
                                    : Vital_RrContinuityQuality(bpm, track_bpm);

        if (is_hr != 0U)
        {
            harmonic_quality = Vital_HrHarmonicQuality(bpm,
                                                       (VitalAxis_t)best_a,
                                                       rr_bpm,
                                                       rr_sqi,
                                                       rr_axis,
                                                       &near_harm,
                                                       &harmonic_k);
        }
        else
        {
            /* Soft fundamental-vs-2x ambiguity. Do not hard halve/double. */
            float half_bpm = 0.5f * bpm;
            if ((bpm >= 20.0f) && (half_bpm >= bpm_min))
            {
                int16_t half_i = (int16_t)((half_bpm - bpm_min) / bpm_step + 0.5f);
                if ((half_i >= 0) && (half_i < bins))
                {
                    float ratio = power[best_a][(uint8_t)half_i] / (p + VITAL_EPS);
                    if (ratio > 0.65f) harmonic_quality = 0.55f;
                    else if (ratio > 0.35f) harmonic_quality = 0.75f;
                }
            }
        }

        snr_quality = Vital_SnrQuality(best_snr, is_hr);
        prom_quality = Vital_ProminenceQuality(prominence);

        if (is_hr != 0U)
        {
            quality = 0.34f * snr_quality +
                      0.20f * prom_quality +
                      0.08f * support +
                      0.25f * continuity +
                      0.13f * harmonic_quality;
        }
        else
        {
            quality = 0.36f * snr_quality +
                      0.24f * prom_quality +
                      0.08f * support +
                      0.24f * continuity +
                      0.08f * harmonic_quality;
        }

        /* Transition evidence deliberately excludes temporal continuity.
         * A genuinely changing vital rate must be allowed to move away from the
         * old track when the frequency itself is spectrally credible. */
        if (is_hr != 0U)
        {
            transition_quality = 0.44f * snr_quality +
                                 0.24f * prom_quality +
                                 0.10f * support +
                                 0.22f * harmonic_quality;
        }
        else
        {
            transition_quality = 0.48f * snr_quality +
                                 0.27f * prom_quality +
                                 0.10f * support +
                                 0.15f * harmonic_quality;
        }

        /* Search edges are more vulnerable to leakage. */
        if ((bpm <= bpm_min + 1.0f) || (bpm >= bpm_max - 1.0f))
        {
            quality *= 0.82f;
            transition_quality *= 0.82f;
        }

        score = quality;
        memset(&cand, 0, sizeof(cand));
        cand.bpm = bpm;
        cand.freq_hz = bpm / 60.0f;
        cand.snr = best_snr;
        cand.peak_power = p;
        cand.noise_power = p / (best_snr + VITAL_EPS);
        cand.prominence = prominence;
        cand.axis_support = support;
        cand.continuity_quality = continuity;
        cand.harmonic_quality = harmonic_quality;
        cand.transition_quality = transition_quality;
        cand.peak_shape_quality = peak_shape_quality;
        cand.score = score;
        cand.sqi = Vital_QualityToSqi(quality);
        cand.transition_sqi = Vital_QualityToSqi(transition_quality * VITAL_SWITCH_QUALITY_SCALE);
        cand.axis = (VitalAxis_t)best_a;
        cand.near_harmonic = near_harm;
        cand.harmonic_k = harmonic_k;

        if ((result.raw_best.axis == VITAL_AXIS_NONE) || (best_snr > result.raw_best.snr)) result.raw_best = cand;
        if ((near_harm == 0U) && ((result.clean_best.axis == VITAL_AXIS_NONE) || (score > result.clean_best.score))) result.clean_best = cand;

        if (is_local_peak != 0U) Vital_InsertCandidate(&result, &cand);
    }

    /* A broad peak can miss strict local-max insertion at an edge; preserve best diagnostic candidate. */
    if ((result.count == 0U) && (result.raw_best.axis != VITAL_AXIS_NONE))
    {
        result.top[0] = result.raw_best;
        result.count = 1U;
    }

    if (is_hr != 0U)
    {
        uint8_t enrich_count = (result.count < VITAL_HR_CANDIDATE_COUNT) ?
                               result.count : VITAL_HR_CANDIDATE_COUNT;
        for (i = 0U; i < enrich_count; i++)
        {
            Vital_EnrichHrCandidate(est, &result.top[i]);
        }
    }

    return result;
}

static float Vital_RateLimitedUpdate(float track, float target, float alpha, float max_step);

static void Vital_ClearHrChallenger(VitalEstimator_t *est)
{
    est->hr_challenger_bpm = 0.0f;
    est->hr_challenger_evidence = 0.0f;
    est->hr_challenger_hits = 0U;
    est->hr_challenger_primary_hits = 0U;
    est->hr_challenger_primary_gap = 0U;
    est->hr_challenger_gap = 0U;
    est->hr_challenger_confirmed = 0U;
    est->hr_challenger_release_hits = 0U;
    est->hr_challenger_release_confirmed = 0U;
}

static int8_t Vital_FindBestHrCandidateNear(const VitalSearchResult_t *search,
                                            float bpm,
                                            float tolerance,
                                            uint8_t require_far_from_track,
                                            float track_bpm)
{
    uint8_t i;
    uint8_t limit = (search->count < VITAL_HR_CANDIDATE_COUNT) ?
                    search->count : VITAL_HR_CANDIDATE_COUNT;
    int8_t selected = -1;
    float selected_quality = -1.0f;

    for (i = 0U; i < limit; i++)
    {
        const VitalCandidate_t *candidate = &search->top[i];
        if (Vital_AbsF(candidate->bpm - bpm) > tolerance) continue;
        if ((require_far_from_track != 0U) &&
            (track_bpm > 0.0f) &&
            (Vital_AbsF(candidate->bpm - track_bpm) <= VITAL_HR_SWITCH_DELTA_BPM))
        {
            continue;
        }
        if (candidate->evidence_quality > selected_quality)
        {
            selected_quality = candidate->evidence_quality;
            selected = (int8_t)i;
        }
    }
    return selected;
}

static int8_t Vital_FindStrongestFarHrCandidate(const VitalSearchResult_t *search,
                                                float track_bpm)
{
    uint8_t i;
    uint8_t limit = (search->count < VITAL_HR_CANDIDATE_COUNT) ?
                    search->count : VITAL_HR_CANDIDATE_COUNT;
    int8_t selected = -1;
    float selected_quality = -1.0f;

    for (i = 0U; i < limit; i++)
    {
        const VitalCandidate_t *candidate = &search->top[i];
        if (Vital_AbsF(candidate->bpm - track_bpm) <= VITAL_HR_SWITCH_DELTA_BPM) continue;
        if (candidate->evidence_quality > selected_quality)
        {
            selected_quality = candidate->evidence_quality;
            selected = (int8_t)i;
        }
    }
    return selected;
}

static uint8_t Vital_HrReleaseEvidence(const VitalCandidate_t *challenger,
                                       const VitalCandidate_t *current,
                                       float track_bpm)
{
    uint8_t recent_challenger_aligned;
    uint8_t current_recently_rejected;
    uint8_t challenger_structurally_sound;

    if (challenger == 0) return 0U;

    /* The fast route releases a stale high path.  Upward changes retain the
     * original primary-candidate confirmation because high mechanical peaks
     * were the dominant false switch in the disruption recording.  Likewise,
     * a path close to the configured physiological lower edge is not allowed
     * to bypass conservative confirmation. */
    if ((challenger->bpm >= track_bpm - 2.0f) ||
        (challenger->bpm < VITAL_HR_MIN_BPM + 10.0f))
    {
        return 0U;
    }

    recent_challenger_aligned =
        (Vital_AbsF(challenger->recent_bpm - challenger->bpm) <=
         VITAL_HR_RELEASE_RECENT_MATCH_BPM) ? 1U : 0U;
    current_recently_rejected =
        ((current == 0) ||
         (Vital_AbsF(current->recent_bpm - track_bpm) >=
          VITAL_HR_RELEASE_RECENT_REJECT_BPM)) ? 1U : 0U;
    challenger_structurally_sound =
        ((challenger->evidence_quality >= 0.50f) &&
         (challenger->recent_consistency >= 0.68f) &&
         (challenger->half_consistency >= 0.58f) &&
         ((challenger->periodicity_quality >= 0.28f) ||
          (challenger->axis_support >= 0.55f))) ? 1U : 0U;

    if ((recent_challenger_aligned == 0U) ||
        (current_recently_rejected == 0U) ||
        (challenger_structurally_sound == 0U))
    {
        return 0U;
    }

    if ((current != 0) &&
        (challenger->evidence_quality + VITAL_HR_RELEASE_EVIDENCE_MARGIN <
         current->evidence_quality))
    {
        return 0U;
    }
    return 1U;
}

static void Vital_UpdateHrChallenger(VitalEstimator_t *est,
                                     const VitalSearchResult_t *search)
{
    int8_t matched = -1;
    int8_t current_matched = -1;
    int8_t strongest_far;
    float primary_margin = 1.0f;
    uint8_t i;
    uint8_t limit = (search->count < VITAL_HR_CANDIDATE_COUNT) ?
                    search->count : VITAL_HR_CANDIDATE_COUNT;

    if (limit > 1U)
    {
        float best_alternative = search->top[1].evidence_quality;
        for (i = 2U; i < limit; i++)
        {
            if (search->top[i].evidence_quality > best_alternative)
            {
                best_alternative = search->top[i].evidence_quality;
            }
        }
        primary_margin = search->top[0].evidence_quality - best_alternative;
    }

    if (est->hr_track_bpm < 1.0f)
    {
        Vital_ClearHrChallenger(est);
        return;
    }

    /* A release-confirmed path stays latched until the conservative spectral
     * anchor also reaches it.  Clearing as soon as the user-facing output got
     * within 8 bpm caused an immediate rebound to the contradicted old path. */
    if ((est->hr_challenger_bpm > 0.0f) &&
        (((est->hr_challenger_release_confirmed == 0U) &&
          (Vital_AbsF(est->hr_challenger_bpm - est->hr_track_bpm) <=
           VITAL_HR_SWITCH_DELTA_BPM)) ||
         ((est->hr_challenger_release_confirmed != 0U) &&
          (est->hr_anchor_track_bpm > 0.0f) &&
          (Vital_AbsF(est->hr_challenger_bpm - est->hr_anchor_track_bpm) <=
           VITAL_HR_RELEASE_CONVERGED_BPM))))
    {
        Vital_ClearHrChallenger(est);
    }

    /* Release decisions are judged against the independent spectral anchor,
     * not against a user-facing track that may still be catching up from the
     * previous release.  This prevents a transient low peak from launching a
     * second release merely because the displayed track is temporarily low. */
    {
        float release_reference = (est->hr_anchor_track_bpm > 0.0f) ?
                                  est->hr_anchor_track_bpm : est->hr_track_bpm;
        strongest_far = Vital_FindStrongestFarHrCandidate(search, release_reference);
        current_matched = Vital_FindBestHrCandidateNear(search,
                                                        release_reference,
                                                        6.0f,
                                                        0U,
                                                        release_reference);
    }
    if (est->hr_challenger_bpm > 0.0f)
    {
        matched = Vital_FindBestHrCandidateNear(search,
                                                est->hr_challenger_bpm,
                                                VITAL_HR_CHALLENGER_MATCH_BPM,
                                                0U,
                                                est->hr_track_bpm);
    }

    if (matched >= 0)
    {
        const VitalCandidate_t *candidate = &search->top[(uint8_t)matched];
        est->hr_challenger_evidence =
            VITAL_HR_CHALLENGER_DECAY * est->hr_challenger_evidence +
            candidate->evidence_quality;
        est->hr_challenger_bpm =
            0.70f * est->hr_challenger_bpm + 0.30f * candidate->bpm;
        if (est->hr_challenger_hits < 255U) est->hr_challenger_hits++;
        if (Vital_HrReleaseEvidence(
                candidate,
                (current_matched >= 0) ? &search->top[(uint8_t)current_matched] : 0,
                (est->hr_anchor_track_bpm > 0.0f) ?
                est->hr_anchor_track_bpm : est->hr_track_bpm) != 0U)
        {
            if (est->hr_challenger_release_hits < 255U)
            {
                est->hr_challenger_release_hits++;
            }
        }
        else if (est->hr_challenger_release_hits > 0U)
        {
            est->hr_challenger_release_hits--;
            if ((est->hr_challenger_release_confirmed != 0U) &&
                (est->hr_challenger_release_hits == 0U))
            {
                Vital_ClearHrChallenger(est);
                return;
            }
        }
        if ((matched == 0) && (est->hr_challenger_primary_hits < 255U))
        {
            est->hr_challenger_primary_hits++;
            est->hr_challenger_primary_gap = 0U;
        }
        else if (matched != 0)
        {
            if (est->hr_challenger_primary_gap < 255U)
            {
                est->hr_challenger_primary_gap++;
            }
            if (est->hr_challenger_primary_gap > VITAL_HR_CHALLENGER_MAX_GAP)
            {
                est->hr_challenger_primary_hits = 0U;
            }
        }
        est->hr_challenger_gap = 0U;
    }
    else if (est->hr_challenger_bpm > 0.0f)
    {
        est->hr_challenger_evidence *= VITAL_HR_CHALLENGER_DECAY;
        if (est->hr_challenger_primary_gap < 255U)
        {
            est->hr_challenger_primary_gap++;
        }
        if (est->hr_challenger_primary_gap > VITAL_HR_CHALLENGER_MAX_GAP)
        {
            est->hr_challenger_primary_hits = 0U;
        }
        if (est->hr_challenger_gap < 255U) est->hr_challenger_gap++;
        if (est->hr_challenger_gap > VITAL_HR_CHALLENGER_MAX_GAP)
        {
            Vital_ClearHrChallenger(est);
        }
    }

    if ((est->hr_challenger_bpm <= 0.0f) && (strongest_far >= 0))
    {
        const VitalCandidate_t *candidate = &search->top[(uint8_t)strongest_far];
        est->hr_challenger_bpm = candidate->bpm;
        est->hr_challenger_evidence = candidate->evidence_quality;
        est->hr_challenger_hits = 1U;
        est->hr_challenger_primary_hits = (strongest_far == 0) ? 1U : 0U;
        est->hr_challenger_primary_gap = (strongest_far == 0) ? 0U : 1U;
        est->hr_challenger_gap = 0U;
        est->hr_challenger_release_hits =
            Vital_HrReleaseEvidence(
                candidate,
                (current_matched >= 0) ? &search->top[(uint8_t)current_matched] : 0,
                (est->hr_anchor_track_bpm > 0.0f) ?
                est->hr_anchor_track_bpm : est->hr_track_bpm);
    }

    /* Normal takeover still requires the challenger to become the primary
     * spectral peak.  The independent release route is deliberately
     * asymmetric: a second/third candidate may take over after two supported
     * appearances only when the recent window contradicts the old track and
     * supports the challenger on structural cardiac evidence. */
    if ((est->hr_challenger_bpm > 0.0f) &&
        (est->hr_challenger_hits >= 2U) &&
        (est->hr_challenger_primary_hits >= 3U) &&
        (est->hr_challenger_gap <= VITAL_HR_CHALLENGER_MAX_GAP) &&
        (est->hr_challenger_evidence >= VITAL_HR_CHALLENGER_CONFIRM) &&
        (search->count > 0U) &&
        (Vital_AbsF(search->top[0].bpm - est->hr_challenger_bpm) <=
         VITAL_HR_CHALLENGER_MATCH_BPM) &&
        (search->top[0].evidence_quality >= 0.55f) &&
        (primary_margin >= VITAL_HR_EVIDENCE_MARGIN_CLEAR) &&
        (est->hr_challenger_evidence >= 0.70f * est->hr_track_support))
    {
        est->hr_challenger_confirmed = 1U;
    }

    if ((est->hr_challenger_bpm > 0.0f) &&
        (est->hr_challenger_hits >= VITAL_HR_RELEASE_HITS_REQUIRED) &&
        (est->hr_challenger_release_hits >= VITAL_HR_RELEASE_HITS_REQUIRED) &&
        (est->hr_challenger_gap <= VITAL_HR_CHALLENGER_MAX_GAP) &&
        (est->hr_challenger_evidence >= VITAL_HR_CHALLENGER_CONFIRM) &&
        (matched >= 0))
    {
        est->hr_challenger_confirmed = 1U;
        est->hr_challenger_release_confirmed = 1U;
    }
}

static VitalHrSelection_t Vital_SelectHrEvidence(VitalEstimator_t *est,
                                                 const VitalSearchResult_t *search,
                                                 uint8_t primary_transition_active)
{
    VitalHrSelection_t selection;
    uint8_t i;
    uint8_t limit = (search->count < VITAL_HR_CANDIDATE_COUNT) ?
                    search->count : VITAL_HR_CANDIDATE_COUNT;
    float best_quality = -1.0f;
    float second_quality = -1.0f;

    memset(&selection, 0, sizeof(selection));
    selection.evidence_candidate = search->top[0];
    selection.target_bpm = search->top[0].bpm;

    for (i = 0U; i < limit; i++)
    {
        float q = search->top[i].evidence_quality;
        if (q > best_quality)
        {
            second_quality = best_quality;
            best_quality = q;
        }
        else if (q > second_quality)
        {
            second_quality = q;
        }
    }
    if (second_quality < 0.0f) second_quality = 0.0f;
    selection.candidate_margin = best_quality - second_quality;

    Vital_UpdateHrChallenger(est, search);
    if ((est->hr_challenger_release_confirmed != 0U) &&
        (est->hr_challenger_bpm > 0.0f))
    {
        int8_t matched = Vital_FindBestHrCandidateNear(search,
                                                       est->hr_challenger_bpm,
                                                       VITAL_HR_SWITCH_DELTA_BPM,
                                                       0U,
                                                       est->hr_track_bpm);
        selection.target_bpm = est->hr_challenger_bpm;
        selection.transition_active = 1U;
        selection.release_active = 1U;
        if (matched >= 0) selection.evidence_candidate = search->top[(uint8_t)matched];
    }
    else if (primary_transition_active != 0U)
    {
        selection.target_bpm = search->top[0].bpm;
        selection.evidence_candidate = search->top[0];
        selection.transition_active = 1U;
    }
    else if ((est->hr_challenger_confirmed != 0U) &&
        (Vital_AbsF(est->hr_challenger_bpm - est->hr_track_bpm) >
         VITAL_HR_SWITCH_DELTA_BPM))
    {
        int8_t matched = Vital_FindBestHrCandidateNear(search,
                                                       est->hr_challenger_bpm,
                                                       VITAL_HR_CHALLENGER_MATCH_BPM,
                                                       0U,
                                                       est->hr_track_bpm);
        selection.target_bpm = est->hr_challenger_bpm;
        selection.transition_active = 1U;
        if (matched >= 0) selection.evidence_candidate = search->top[(uint8_t)matched];
    }
    else if ((limit > 1U) &&
             (selection.candidate_margin < VITAL_HR_EVIDENCE_MARGIN_CLEAR))
    {
        float centre = (est->hr_track_bpm > 0.0f) ?
                       est->hr_track_bpm : search->top[0].bpm;
        float weighted_bpm = 0.0f;
        float weight_sum = 0.0f;

        for (i = 0U; i < limit; i++)
        {
            const VitalCandidate_t *candidate = &search->top[i];
            float weight;
            if (Vital_AbsF(candidate->bpm - centre) > VITAL_HR_CONSENSUS_RADIUS_BPM) continue;
            weight = expf(4.0f * (candidate->evidence_quality - best_quality));
            weighted_bpm += weight * candidate->bpm;
            weight_sum += weight;
        }

        if (weight_sum > VITAL_EPS)
        {
            float nearest_distance = 1.0e9f;
            selection.target_bpm = weighted_bpm / weight_sum;
            selection.consensus_used = 1U;
            for (i = 0U; i < limit; i++)
            {
                float distance = Vital_AbsF(search->top[i].bpm - selection.target_bpm);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    selection.evidence_candidate = search->top[i];
                }
            }
        }
    }

    return selection;
}

static float Vital_UpdateHrTrackV18(VitalEstimator_t *est,
                                    float target_bpm,
                                    float target_quality,
                                    uint8_t transition_active,
                                    uint8_t release_active)
{
    float delta;
    float alpha;
    float max_step = VITAL_HR_MAX_STEP_BPM_PER_EST;

    if (est->hr_track_bpm < 1.0f)
    {
        if (target_quality < 0.45f)
        {
            est->hr_init_count = 0U;
            est->hr_pending_bpm = 0.0f;
            return 0.0f;
        }
        if ((est->hr_pending_bpm > 1.0f) &&
            (Vital_AbsF(target_bpm - est->hr_pending_bpm) <= 4.0f))
        {
            if (est->hr_init_count < 255U) est->hr_init_count++;
            est->hr_pending_bpm = 0.5f * (est->hr_pending_bpm + target_bpm);
        }
        else
        {
            est->hr_pending_bpm = target_bpm;
            est->hr_init_count = 1U;
        }
        if (est->hr_init_count >= VITAL_TRACK_INIT_COUNT_REQUIRED)
        {
            est->hr_track_bpm = est->hr_pending_bpm;
            est->hr_pending_bpm = 0.0f;
            est->hr_init_count = 0U;
        }
        return est->hr_track_bpm;
    }

    delta = Vital_AbsF(target_bpm - est->hr_track_bpm);
    if ((delta > VITAL_HR_SWITCH_DELTA_BPM) && (transition_active == 0U))
    {
        return est->hr_track_bpm;
    }

    if (release_active != 0U)
    {
        alpha = 0.55f;
        max_step = VITAL_HR_RELEASE_STEP_BPM;
    }
    else if (transition_active != 0U)
    {
        alpha = 0.42f;
        max_step = VITAL_HR_TRANSITION_STEP_BPM;
    }
    else if (delta <= 2.0f) alpha = 0.55f;
    else alpha = 0.32f;

    est->hr_track_bpm = Vital_RateLimitedUpdate(est->hr_track_bpm,
                                                target_bpm,
                                                alpha,
                                                max_step);
    return est->hr_track_bpm;
}

static float Vital_RateLimitedUpdate(float track, float target, float alpha, float max_step)
{
    float step = alpha * (target - track);
    step = Vital_ClampF(step, -max_step, max_step);
    return track + step;
}

static float Vital_UpdateTrack(float track,
                               float candidate,
                               uint8_t candidate_sqi,
                               float switch_delta,
                               uint8_t pending_required,
                               float max_step,
                               float *pending_bpm,
                               uint8_t *pending_count,
                               uint8_t *init_count,
                               uint8_t is_rr)
{
    float delta;
    float target;
    float alpha;

    if ((pending_bpm == 0) || (pending_count == 0) || (init_count == 0)) return track;

    if (track < 1.0f)
    {
        if (candidate_sqi < 48U)
        {
            *init_count = 0U;
            return 0.0f;
        }

        if ((*pending_bpm > 1.0f) && (Vital_AbsF(candidate - *pending_bpm) <= 3.0f))
        {
            if (*init_count < 255U) (*init_count)++;
            *pending_bpm = 0.5f * (*pending_bpm + candidate);
        }
        else
        {
            *pending_bpm = candidate;
            *init_count = 1U;
        }

        if (*init_count >= VITAL_TRACK_INIT_COUNT_REQUIRED)
        {
            track = *pending_bpm;
            *pending_bpm = 0.0f;
            *pending_count = 0U;
            *init_count = 0U;
        }
        return track;
    }

    if (candidate_sqi < 35U) return track;

    delta = Vital_AbsF(candidate - track);
    target = candidate;

    if (delta > switch_delta)
    {
        if ((*pending_bpm > 1.0f) && (Vital_AbsF(candidate - *pending_bpm) <= 3.0f))
        {
            if (*pending_count < 255U) (*pending_count)++;
            *pending_bpm = 0.65f * (*pending_bpm) + 0.35f * candidate;
        }
        else
        {
            *pending_bpm = candidate;
            *pending_count = 1U;
        }

        if ((*pending_count < pending_required) || (candidate_sqi < VITAL_TRANSITION_CONFIRM_SQI_MIN))
        {
            target = track;
        }
        else
        {
            /* Once a distant candidate has been confirmed, keep following it
             * at the configured rate limit. Do not require a fresh confirmation
             * after every one-second tracker step. */
            target = *pending_bpm;
            *pending_count = pending_required;
        }
    }
    else
    {
        *pending_count = 0U;
        *pending_bpm = 0.0f;
    }

    delta = Vital_AbsF(target - track);
    if (is_rr != 0U)
    {
        if (delta <= 2.0f) alpha = 0.35f;
        else if (delta <= switch_delta) alpha = 0.20f;
        else alpha = 0.30f;
    }
    else
    {
        if (delta <= 2.0f) alpha = 0.55f;
        else if (delta <= switch_delta) alpha = 0.32f;
        else alpha = 0.30f;
    }

    return Vital_RateLimitedUpdate(track, target, alpha, max_step);
}

void VitalEstimator_Init(VitalEstimator_t *est)
{
    if (est == 0) return;
    memset(est, 0, sizeof(*est));
    est->motion_quality = 1.0f;
    est->motion_class = VITAL_MOTION_CLEAN;
    est->last_output.rr_axis = VITAL_AXIS_NONE;
    est->last_output.hr_axis = VITAL_AXIS_NONE;
    est->last_output.hr_status = VITAL_HR_STATUS_NONE;
    est->hr_evidence_axis = VITAL_AXIS_NONE;
}

void VitalEstimator_Reset(VitalEstimator_t *est)
{
    VitalEstimator_Init(est);
}

void VitalEstimator_PushSample(VitalEstimator_t *est, const ImuSample_t *sample)
{
    float dx, dy, dz;
    int16_t qx, qy, qz;

    if ((est == 0) || (sample == 0)) return;

    if (est->gravity_initialised == 0U)
    {
        est->gravity_x = sample->ax;
        est->gravity_y = sample->ay;
        est->gravity_z = sample->az;
        est->gravity_initialised = 1U;
    }

    est->gravity_x += VITAL_GRAVITY_ALPHA * (sample->ax - est->gravity_x);
    est->gravity_y += VITAL_GRAVITY_ALPHA * (sample->ay - est->gravity_y);
    est->gravity_z += VITAL_GRAVITY_ALPHA * (sample->az - est->gravity_z);

    dx = sample->ax - est->gravity_x;
    dy = sample->ay - est->gravity_y;
    dz = sample->az - est->gravity_z;

    est->aa_x += VITAL_AA_ALPHA * (dx - est->aa_x);
    est->aa_y += VITAL_AA_ALPHA * (dy - est->aa_y);
    est->aa_z += VITAL_AA_ALPHA * (dz - est->aa_z);

    est->decim_count++;
    if (est->decim_count < VITAL_DECIMATION_FACTOR) return;
    est->decim_count = 0U;

    qx = Vital_ClampI16FromFloat(est->aa_x * VITAL_Q_SCALE);
    qy = Vital_ClampI16FromFloat(est->aa_y * VITAL_Q_SCALE);
    qz = Vital_ClampI16FromFloat(est->aa_z * VITAL_Q_SCALE);

    est->x[est->write_index] = qx;
    est->y[est->write_index] = qy;
    est->z[est->write_index] = qz;
    est->write_index++;
    if (est->write_index >= VITAL_WINDOW_SAMPLES) est->write_index = 0U;
    if (est->count < VITAL_WINDOW_SAMPLES) est->count++;
}

void VitalEstimator_SetMotionContext(VitalEstimator_t *est, const RestDetectorOutput_t *rest_out)
{
    const RestShortFeatures_t *f;
    float quality = 1.0f;
    VitalMotionClass_t motion_class = VITAL_MOTION_CLEAN;

    if ((est == 0) || (rest_out == 0)) return;
    f = &rest_out->features_2s;

    if (rest_out->strong_motion_detected)
    {
        quality = 0.0f;
        motion_class = VITAL_MOTION_STRONG;
        est->motion_hold_seconds = VITAL_MOTION_RECOVERY_HOLD_SEC;
    }
    else if ((f->acc_mag_range >= 0.25f) ||
             (f->acc_jerk_p95 >= 3.5f) ||
             (f->gyr_mag_p95 >= 35.0f))
    {
        quality = 0.18f;
        motion_class = VITAL_MOTION_MODERATE;
        est->motion_hold_seconds = VITAL_MOTION_RECOVERY_HOLD_SEC;
    }
    else if ((rest_out->local_label == REST_LOCAL_DISTURBANCE) ||
             (f->acc_mag_range >= 0.05f) ||
             (f->acc_jerk_p95 >= 1.5f) ||
             (f->gyr_mag_p95 >= 8.0f))
    {
        quality = 0.48f;
        motion_class = VITAL_MOTION_MILD;
        if (est->motion_hold_seconds < 2U) est->motion_hold_seconds = 2U;
    }
    else if ((f->gyr_mag_p95 >= 3.0f) || (f->gyr_mag_rms >= 1.8f))
    {
        /* Sustained low-amplitude sway: remain measurable with a small SQI cost. */
        quality = 0.90f;
        motion_class = VITAL_MOTION_MILD;
    }

    if ((motion_class < VITAL_MOTION_MODERATE) && (est->motion_hold_seconds > 0U))
    {
        const uint8_t remaining = est->motion_hold_seconds;
        const float recovery_cap =
            (remaining > (VITAL_MOTION_RECOVERY_HOLD_SEC / 2U)) ? 0.38f : 0.58f;
        est->motion_hold_seconds--;
        if (quality > recovery_cap) quality = recovery_cap;
        motion_class = VITAL_MOTION_MILD;
    }

    est->motion_quality = quality;
    est->motion_class = motion_class;
}

bool VitalEstimator_Estimate(VitalEstimator_t *est, VitalOutput_t *out)
{
    VitalSearchResult_t rr_search;
    VitalSearchResult_t hr_search;
    VitalCandidate_t rr_cand;
    VitalCandidate_t hr_cand;
    VitalHrSelection_t hr_selection;
    uint8_t i;
    float rr_quality;
    uint8_t rr_switch_sqi = 0U;
    uint8_t rr_transition_active = 0U;
    uint8_t hr_transition_active = 0U;
    uint8_t hr_primary_switch_sqi = 0U;
    uint8_t hr_primary_transition_active = 0U;

    if ((est == 0) || (out == 0)) return false;

    memset(out, 0, sizeof(*out));
    out->rr_axis = VITAL_AXIS_NONE;
    out->hr_axis = VITAL_AXIS_NONE;
    out->hr_status = VITAL_HR_STATUS_NONE;
    for (i = 0U; i < VITAL_HR_CANDIDATE_COUNT; i++)
    {
        out->hr_cand_axis[i] = VITAL_AXIS_NONE;
    }
    out->stored_samples = est->count;
    out->window_fill_percent = (uint8_t)(((uint32_t)est->count * 100U) / VITAL_WINDOW_SAMPLES);
    out->motion_quality = est->motion_quality;
    out->motion_class = est->motion_class;

    if (est->count < VITAL_WINDOW_SAMPLES)
    {
        est->last_output = *out;
        return false;
    }

    rr_search = Vital_SearchBand(est,
                                 VITAL_RR_WINDOW_SAMPLES,
                                 VITAL_RR_MIN_BPM,
                                 VITAL_RR_MAX_BPM,
                                 VITAL_RR_STEP_BPM,
                                 0U,
                                 0U,
                                 est->rr_track_bpm,
                                 0.0f,
                                 0U,
                                 VITAL_AXIS_NONE);

    rr_cand = rr_search.top[0];
    rr_quality = ((float)rr_cand.sqi / 100.0f) * est->motion_quality;
    rr_cand.sqi = Vital_QualityToSqi(rr_quality);

    {
        const uint8_t rr_base_sqi = rr_cand.sqi;
        rr_switch_sqi =
            Vital_QualityToSqi(((float)rr_cand.transition_sqi / 100.0f) * est->motion_quality);

        est->rr_track_bpm = Vital_UpdateTrack(est->rr_track_bpm,
                                          rr_cand.bpm,
                                          rr_switch_sqi,
                                          VITAL_RR_SWITCH_DELTA_BPM,
                                          VITAL_RR_PENDING_COUNT_REQUIRED,
                                          VITAL_RR_MAX_STEP_BPM_PER_EST,
                                          &est->rr_pending_bpm,
                                          &est->rr_pending_count,
                                          &est->rr_init_count,
                                          1U);

        rr_transition_active =
            ((est->rr_pending_count >= VITAL_RR_PENDING_COUNT_REQUIRED) &&
             (rr_switch_sqi >= VITAL_TRANSITION_CONFIRM_SQI_MIN)) ? 1U : 0U;

        if (rr_transition_active != 0U)
        {
            float trend_q =
                VITAL_TRANSITION_SQI_BLEND * ((float)rr_switch_sqi / 100.0f) +
                (1.0f - VITAL_TRANSITION_SQI_BLEND) * ((float)rr_base_sqi / 100.0f);
            rr_cand.sqi = Vital_QualityToSqi(trend_q);
        }
        else if (est->rr_track_bpm > 0.0f)
        {
            float agreement = Vital_ClampF(
                1.0f - Vital_AbsF(est->rr_track_bpm - rr_cand.bpm) / 8.0f,
                0.25f,
                1.0f);
            rr_cand.sqi =
                Vital_QualityToSqi(((float)rr_base_sqi / 100.0f) * agreement);
        }
    }

    out->rr_candidate_bpm = rr_cand.bpm;
    out->rr_raw_bpm = rr_search.raw_best.bpm;
    out->rr_alt_bpm = (rr_search.count > 1U) ? rr_search.top[1].bpm : 0.0f;
    out->rr_corrected = 0U;
    out->rr_energy_low = (rr_cand.peak_power < VITAL_RR_PEAK_POWER_MIN) ? 1U : 0U;
    out->rr_bpm = est->rr_track_bpm;
    out->rr_peak_freq_hz = (est->rr_track_bpm > 0.0f) ? est->rr_track_bpm / 60.0f : 0.0f;
    out->rr_snr = rr_cand.snr;
    out->rr_sqi = rr_cand.sqi;
    out->rr_axis = rr_cand.axis;
    out->rr_prominence = rr_cand.prominence;
    out->rr_axis_support = rr_cand.axis_support;
    out->rr_continuity_quality = rr_cand.continuity_quality;
    out->rr_harmonic_quality = rr_cand.harmonic_quality;
    out->rr_valid = ((out->rr_bpm > 0.0f) &&
                     (out->rr_sqi >= VITAL_RR_VALID_SQI_MIN) &&
                     (out->rr_energy_low == 0U)) ? 1U : 0U;
    out->rr_trend_active = rr_transition_active;

    /* Reportability is intentionally separate from strict validity.
     * A persistent, rate-limited transition remains visible, while a single
     * unsupported candidate jump still cannot create an output. */
    if ((est->motion_class >= VITAL_MOTION_MODERATE) ||
        (out->rr_bpm <= 0.0f) ||
        (out->rr_energy_low != 0U))
    {
        est->rr_report_hold_seconds = 0U;
        out->rr_reportable = 0U;
    }
    else
    {
        if ((out->rr_valid != 0U) || (rr_transition_active != 0U))
        {
            est->rr_report_hold_seconds = VITAL_REPORT_HOLD_SEC;
        }
        else if (est->rr_report_hold_seconds > 0U)
        {
            est->rr_report_hold_seconds--;
        }

        out->rr_reportable =
            ((out->rr_valid != 0U) ||
             (rr_transition_active != 0U) ||
             (est->rr_report_hold_seconds > 0U)) ? 1U : 0U;
    }

    hr_search = Vital_SearchBand(est,
                                 VITAL_HR_WINDOW_SAMPLES,
                                 VITAL_HR_MIN_BPM,
                                 VITAL_HR_MAX_BPM,
                                 VITAL_HR_STEP_BPM,
                                 1U,
                                 1U,
                                 est->hr_anchor_track_bpm,
                                 (out->rr_bpm > 0.0f) ? out->rr_bpm : rr_cand.bpm,
                                 out->rr_sqi,
                                 out->rr_axis);

    hr_primary_switch_sqi = Vital_QualityToSqi(
        ((float)hr_search.top[0].transition_sqi / 100.0f) * est->motion_quality);
    est->hr_anchor_track_bpm = Vital_UpdateTrack(est->hr_anchor_track_bpm,
                                                 hr_search.top[0].bpm,
                                                 hr_primary_switch_sqi,
                                                 VITAL_HR_SWITCH_DELTA_BPM,
                                                 VITAL_HR_PENDING_COUNT_REQUIRED,
                                                 VITAL_HR_MAX_STEP_BPM_PER_EST,
                                                 &est->hr_anchor_pending_bpm,
                                                 &est->hr_anchor_pending_count,
                                                 &est->hr_anchor_init_count,
                                                 0U);
    hr_primary_transition_active =
        ((est->hr_anchor_pending_count >= VITAL_HR_PENDING_COUNT_REQUIRED) &&
         (hr_primary_switch_sqi >= VITAL_TRANSITION_CONFIRM_SQI_MIN)) ? 1U : 0U;

    hr_selection = Vital_SelectHrEvidence(est,
                                          &hr_search,
                                          hr_primary_transition_active);
    hr_cand = hr_selection.evidence_candidate;
    hr_transition_active = hr_selection.transition_active;

    Vital_UpdateHrTrackV18(est,
                           hr_selection.target_bpm,
                           hr_cand.evidence_quality * est->motion_quality,
                           hr_transition_active,
                           hr_selection.release_active);

    {
        float support_bpm = (hr_transition_active != 0U) ?
                            hr_selection.target_bpm : est->hr_track_bpm;
        float current_support = 0.0f;
        float history_quality;
        float axis_stability;
        float margin_quality;
        float output_agreement;
        float instant_rr_suspicion;
        float base_quality;
        float evidence_factor;
        float sqi_quality;
        uint8_t limit = (hr_search.count < VITAL_HR_CANDIDATE_COUNT) ?
                        hr_search.count : VITAL_HR_CANDIDATE_COUNT;

        for (i = 0U; i < limit; i++)
        {
            if ((Vital_AbsF(hr_search.top[i].bpm - support_bpm) <= 6.0f) &&
                (hr_search.top[i].evidence_quality > current_support))
            {
                current_support = hr_search.top[i].evidence_quality;
            }
        }
        est->hr_track_support =
            0.82f * est->hr_track_support + current_support;
        history_quality = Vital_ClampF(est->hr_track_support / 2.60f, 0.0f, 1.0f);

        if (est->hr_evidence_axis == VITAL_AXIS_NONE)
        {
            est->hr_evidence_axis = hr_cand.axis;
            est->hr_evidence_axis_age = 1U;
            axis_stability = 0.75f;
        }
        else if (est->hr_evidence_axis == hr_cand.axis)
        {
            if (est->hr_evidence_axis_age < 255U) est->hr_evidence_axis_age++;
            axis_stability = (est->hr_evidence_axis_age >= 3U) ? 1.0f : 0.85f;
        }
        else
        {
            axis_stability = (hr_cand.axis_support >= 0.55f) ? 0.82f : 0.55f;
            est->hr_evidence_axis = hr_cand.axis;
            est->hr_evidence_axis_age = 1U;
        }
        if (hr_cand.axis_support > axis_stability) axis_stability = hr_cand.axis_support;

        margin_quality = Vital_ClampF(
            (hr_selection.candidate_margin + 0.02f) / 0.18f,
            0.0f,
            1.0f);
        output_agreement = Vital_ClampF(
            1.0f - Vital_AbsF(est->hr_track_bpm - hr_selection.target_bpm) / 18.0f,
            0.0f,
            1.0f);
        instant_rr_suspicion = (1.0f - hr_cand.harmonic_quality) *
            Vital_ClampF((float)out->rr_sqi / 75.0f, 0.0f, 1.0f) *
            ((hr_cand.axis == out->rr_axis) ? 1.0f : 0.55f) *
            (0.55f + 0.45f * (1.0f - hr_cand.periodicity_quality));
        est->hr_rr_suspicion =
            0.82f * est->hr_rr_suspicion + 0.18f * instant_rr_suspicion;

        /* Preserve V15's well-calibrated base SQI and use the new evidence to
         * grade it.  An additive score made several mediocre components look
         * overconfident when summed; this multiplicative form cannot turn a
         * weak cardiac candidate into high SQI merely because its axis/history
         * happen to be stable. */
        base_quality = (float)hr_cand.sqi / 100.0f;
        if (hr_transition_active != 0U)
        {
            base_quality = 0.85f * hr_cand.evidence_quality +
                           0.15f * base_quality;
        }
        /* SQI now rewards cardiac structure and recent support directly.
         * Historical support and output agreement can no longer keep a stale
         * high-SNR path confident.  Output agreement is a multiplicative
         * trajectory check, so a rate-limited value far from current spectral
         * evidence is correctly labelled low-confidence while it catches up. */
        evidence_factor =
            0.54f +
            0.11f * hr_cand.half_consistency +
            0.13f * hr_cand.recent_consistency +
            0.07f * hr_cand.peak_shape_quality +
            0.06f * hr_cand.periodicity_quality +
            0.03f * margin_quality +
            0.03f * axis_stability +
            0.03f * history_quality;
        sqi_quality = base_quality * evidence_factor *
                      (0.60f + 0.40f * output_agreement) -
                      0.10f * est->hr_rr_suspicion;
        sqi_quality *= est->motion_quality;
        hr_cand.sqi = Vital_QualityToSqi(sqi_quality);

        out->hr_candidate_margin = hr_selection.candidate_margin;
        out->hr_periodicity_quality = hr_cand.periodicity_quality;
        out->hr_half_consistency = hr_cand.half_consistency;
        out->hr_recent_consistency = hr_cand.recent_consistency;
        out->hr_peak_shape_quality = hr_cand.peak_shape_quality;
        out->hr_history_quality = history_quality;
        out->hr_axis_stability = axis_stability;
        out->hr_output_agreement = output_agreement;
        out->hr_rr_suspicion = est->hr_rr_suspicion;
        out->hr_challenger_bpm = est->hr_challenger_bpm;
        out->hr_challenger_evidence = est->hr_challenger_evidence;
        out->hr_consensus_used = hr_selection.consensus_used;
    }

    out->hr_candidate_bpm = hr_selection.target_bpm;
    out->hr_bpm = est->hr_track_bpm;
    out->hr_tracked_bpm = est->hr_track_bpm;
    out->hr_peak_freq_hz = (est->hr_track_bpm > 0.0f) ? est->hr_track_bpm / 60.0f : 0.0f;
    out->hr_snr = hr_cand.snr;
    out->hr_sqi = hr_cand.sqi;
    out->hr_axis = hr_cand.axis;
    out->hr_raw_bpm = hr_search.raw_best.bpm;
    out->hr_raw_snr = hr_search.raw_best.snr;
    out->hr_clean_bpm = hr_search.clean_best.bpm;
    out->hr_clean_snr = hr_search.clean_best.snr;
    out->hr_near_harmonic = hr_cand.near_harmonic;
    out->hr_harmonic_k = hr_cand.harmonic_k;
    out->hr_prominence = hr_cand.prominence;
    out->hr_axis_support = hr_cand.axis_support;
    out->hr_continuity_quality = hr_cand.continuity_quality;
    out->hr_harmonic_quality = hr_cand.harmonic_quality;

    for (i = 0U; i < VITAL_HR_CANDIDATE_COUNT; i++)
    {
        if (i < hr_search.count)
        {
            out->hr_cand_bpm[i] = hr_search.top[i].bpm;
            out->hr_cand_snr[i] = hr_search.top[i].snr;
            out->hr_cand_harm_k[i] = hr_search.top[i].harmonic_k;
            out->hr_cand_sqi[i] = hr_search.top[i].sqi;
            out->hr_cand_transition_sqi[i] = hr_search.top[i].transition_sqi;
            out->hr_cand_axis[i] = hr_search.top[i].axis;
            out->hr_cand_prominence[i] = hr_search.top[i].prominence;
            out->hr_cand_axis_support[i] = hr_search.top[i].axis_support;
            out->hr_cand_harmonic_quality[i] = hr_search.top[i].harmonic_quality;
            out->hr_cand_periodicity[i] = hr_search.top[i].periodicity_quality;
            out->hr_cand_half_consistency[i] = hr_search.top[i].half_consistency;
            out->hr_cand_recent_consistency[i] = hr_search.top[i].recent_consistency;
            out->hr_cand_peak_shape[i] = hr_search.top[i].peak_shape_quality;
            out->hr_cand_recent_bpm[i] = hr_search.top[i].recent_bpm;
        }
    }

    out->hr_trend_active = hr_transition_active;

    if ((est->motion_class >= VITAL_MOTION_MODERATE) && (out->hr_bpm > 0.0f))
    {
        est->hr_report_hold_seconds = 0U;
        out->hr_valid = 0U;
        out->hr_reportable = 0U;
        out->hr_status = VITAL_HR_STATUS_CONTAM;
    }
    else if ((out->hr_bpm > 0.0f) && (out->hr_sqi >= VITAL_HR_VALID_SQI_MIN))
    {
        est->hr_report_hold_seconds = VITAL_REPORT_HOLD_SEC;
        out->hr_valid = 1U;
        out->hr_reportable = 1U;
        out->hr_status = VITAL_HR_STATUS_VALID;
    }
    else if ((out->hr_bpm > 0.0f) && (out->hr_sqi >= VITAL_HR_VALID_LOW_SQI_MIN))
    {
        est->hr_report_hold_seconds = VITAL_REPORT_HOLD_SEC;
        out->hr_valid = 0U;
        out->hr_reportable = 1U;
        out->hr_status = VITAL_HR_STATUS_VALID_LOW;
    }
    else if ((out->hr_bpm > 0.0f) && (hr_transition_active != 0U))
    {
        est->hr_report_hold_seconds = VITAL_REPORT_HOLD_SEC;
        out->hr_valid = 0U;
        out->hr_reportable = 1U;
        out->hr_status = VITAL_HR_STATUS_TREND;
    }
    else if ((out->hr_bpm > 0.0f) && (est->hr_report_hold_seconds > 0U))
    {
        est->hr_report_hold_seconds--;
        out->hr_valid = 0U;
        out->hr_reportable = 1U;
        out->hr_status = VITAL_HR_STATUS_TREND;
    }
    else
    {
        est->hr_report_hold_seconds = 0U;
        out->hr_valid = 0U;
        out->hr_reportable = 0U;
        out->hr_status = VITAL_HR_STATUS_NONE;
    }

    est->last_output = *out;
    return true;
}

const char *VitalEstimator_AxisToString(VitalAxis_t axis)
{
    switch (axis)
    {
        case VITAL_AXIS_X: return "X";
        case VITAL_AXIS_Y: return "Y";
        case VITAL_AXIS_Z: return "Z";
        default: return "NONE";
    }
}

const char *VitalEstimator_HrStatusToString(VitalHrStatus_t status)
{
    switch (status)
    {
        case VITAL_HR_STATUS_VALID: return "VALID";
        case VITAL_HR_STATUS_VALID_LOW: return "VALID_LOW";
        case VITAL_HR_STATUS_CONTAM: return "CONTAM";
        case VITAL_HR_STATUS_TREND: return "TREND";
        default: return "----";
    }
}

const char *VitalEstimator_MotionClassToString(VitalMotionClass_t motion_class)
{
    switch (motion_class)
    {
        case VITAL_MOTION_CLEAN: return "CLEAN";
        case VITAL_MOTION_MILD: return "MILD";
        case VITAL_MOTION_MODERATE: return "MODERATE";
        case VITAL_MOTION_STRONG: return "STRONG";
        default: return "UNKNOWN";
    }
}
