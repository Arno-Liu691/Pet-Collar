#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv_reader.h"
#include "rest_detector.h"
#include "vital_estimator.h"

typedef struct
{
    RestDetector_t rest;
    VitalEstimator_t vital;
    uint64_t sample_count;
    uint64_t second_count;
    RestState_t previous_state;
    uint8_t vital_was_running;
    uint8_t reset_since_last_second;
} PcPipeline_t;

static void PrintUsage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s input.csv output.csv [--acc-unit g|mps2] [--gyro-unit dps|rads]\n\n"
            "Required CSV headers: ax, ay, az, gx, gy, gz.\n"
            "Optional headers: time_s (or timestamp), sample_index.\n"
            "The baseline algorithm requires uniformly sampled 100 Hz input.\n",
            program);
}

static const char *RrCorrectionToString(uint8_t correction)
{
    switch (correction)
    {
        case 1U: return "x2";
        case 2U: return "/2";
        case 3U: return "lock";
        default: return "none";
    }
}

static void WriteHeader(FILE *out)
{
    fprintf(out,
            "sample_count,algorithm_time_s,input_time_s,input_sample_index,"
            "rest_update_ok,rest_state,local_label,clean_ratio_10s,clean_ratio_20s,"
            "consecutive_disturbance,strong_motion,"
            "acc_mag_mean,acc_mag_std,acc_mag_range,"
            "gyr_mag_mean,gyr_mag_rms,gyr_mag_p95,gyr_mag_max,"
            "acc_jerk_rms,acc_jerk_p95,"
            "entered_measurement,left_measurement,vital_reset_since_previous_second,"
            "vital_estimate_called,vital_estimate_returned,vital_stored_samples,vital_fill_percent,"
            "rr_valid,rr_reportable,rr_trend_active,rr_bpm,rr_raw_bpm,rr_alt_bpm,rr_correction,rr_energy_low,rr_sqi,rr_snr,rr_axis,rr_peak_freq_hz,"
            "hr_status,hr_valid,hr_reportable,hr_trend_active,hr_bpm,hr_tracked_bpm,hr_sqi,hr_snr,hr_axis,"
            "hr_raw_bpm,hr_raw_snr,hr_clean_bpm,hr_clean_snr,hr_near_harmonic,hr_harmonic_k,"
            "hr_cand1_bpm,hr_cand1_snr,hr_cand1_harm_k,"
            "hr_cand2_bpm,hr_cand2_snr,hr_cand2_harm_k,"
            "hr_cand3_bpm,hr_cand3_snr,hr_cand3_harm_k,"
            "hr_cand1_sqi,hr_cand1_transition_sqi,hr_cand1_axis,hr_cand1_prominence,hr_cand1_axis_support,hr_cand1_harmonic_quality,hr_cand1_periodicity,hr_cand1_half_consistency,hr_cand1_recent_consistency,hr_cand1_peak_shape,hr_cand1_recent_bpm,"
            "hr_cand2_sqi,hr_cand2_transition_sqi,hr_cand2_axis,hr_cand2_prominence,hr_cand2_axis_support,hr_cand2_harmonic_quality,hr_cand2_periodicity,hr_cand2_half_consistency,hr_cand2_recent_consistency,hr_cand2_peak_shape,hr_cand2_recent_bpm,"
            "hr_cand3_sqi,hr_cand3_transition_sqi,hr_cand3_axis,hr_cand3_prominence,hr_cand3_axis_support,hr_cand3_harmonic_quality,hr_cand3_periodicity,hr_cand3_half_consistency,hr_cand3_recent_consistency,hr_cand3_peak_shape,hr_cand3_recent_bpm,"
            "motion_quality,motion_class,"
            "rr_candidate_bpm,rr_prominence,rr_axis_support,rr_continuity_quality,rr_harmonic_quality,"
            "hr_candidate_bpm,hr_prominence,hr_axis_support,hr_continuity_quality,hr_harmonic_quality,"
            "hr_candidate_margin,hr_periodicity_quality,hr_half_consistency,hr_recent_consistency,hr_peak_shape_quality,"
            "hr_history_quality,hr_axis_stability,hr_output_agreement,hr_rr_suspicion,"
            "hr_challenger_bpm,hr_challenger_evidence,hr_consensus_used\n");
}

static void WriteSecondRow(FILE *out,
                           const PcPipeline_t *pipeline,
                           const CsvReader_t *reader,
                           bool rest_update_ok,
                           const RestDetectorOutput_t *rest_out,
                           bool entered_measurement,
                           bool left_measurement,
                           bool vital_estimate_called,
                           bool vital_estimate_returned,
                           const VitalOutput_t *vital_out)
{
    const double algorithm_time_s = (double)pipeline->sample_count / (double)REST_FS_HZ;

    fprintf(out, "%llu,%.9f,",
            (unsigned long long)pipeline->sample_count,
            algorithm_time_s);

    if (reader->has_time)
        fprintf(out, "%.9f,", reader->current_time_s);
    else
        fprintf(out, ",");

    if (reader->has_sample_index)
        fprintf(out, "%llu,", (unsigned long long)reader->current_input_index);
    else
        fprintf(out, ",");

    fprintf(out,
            "%u,%s,%s,%.6f,%.6f,%u,%u,"
            "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
            "%u,%u,%u,%u,%u,%u,%u,",
            rest_update_ok ? 1U : 0U,
            RestDetector_StateToString(rest_out->state),
            RestDetector_LocalLabelToString(rest_out->local_label),
            rest_out->clean_ratio_10s,
            rest_out->clean_ratio_20s,
            (unsigned int)rest_out->consecutive_disturbance_count,
            rest_out->strong_motion_detected ? 1U : 0U,
            rest_out->features_2s.acc_mag_mean,
            rest_out->features_2s.acc_mag_std,
            rest_out->features_2s.acc_mag_range,
            rest_out->features_2s.gyr_mag_mean,
            rest_out->features_2s.gyr_mag_rms,
            rest_out->features_2s.gyr_mag_p95,
            rest_out->features_2s.gyr_mag_max,
            rest_out->features_2s.acc_jerk_rms,
            rest_out->features_2s.acc_jerk_p95,
            entered_measurement ? 1U : 0U,
            left_measurement ? 1U : 0U,
            (unsigned int)pipeline->reset_since_last_second,
            vital_estimate_called ? 1U : 0U,
            vital_estimate_returned ? 1U : 0U,
            (unsigned int)pipeline->vital.count,
            (unsigned int)(((uint32_t)pipeline->vital.count * 100U) / VITAL_WINDOW_SAMPLES));

    if (vital_estimate_called)
    {
        fprintf(out,
                "%u,%u,%u,%.6f,%.6f,%.6f,%s,%u,%u,%.9g,%s,%.9g,"
                "%s,%u,%u,%u,%.6f,%.6f,%u,%.9g,%s,"
                "%.6f,%.9g,%.6f,%.9g,%u,%u,"
                "%.6f,%.9g,%u,%.6f,%.9g,%u,%.6f,%.9g,%u,"
                "%u,%u,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%u,%u,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%u,%u,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%s,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",
                (unsigned int)vital_out->rr_valid,
                (unsigned int)vital_out->rr_reportable,
                (unsigned int)vital_out->rr_trend_active,
                vital_out->rr_bpm,
                vital_out->rr_raw_bpm,
                vital_out->rr_alt_bpm,
                RrCorrectionToString(vital_out->rr_corrected),
                (unsigned int)vital_out->rr_energy_low,
                (unsigned int)vital_out->rr_sqi,
                vital_out->rr_snr,
                VitalEstimator_AxisToString(vital_out->rr_axis),
                vital_out->rr_peak_freq_hz,
                VitalEstimator_HrStatusToString(vital_out->hr_status),
                (unsigned int)vital_out->hr_valid,
                (unsigned int)vital_out->hr_reportable,
                (unsigned int)vital_out->hr_trend_active,
                vital_out->hr_bpm,
                vital_out->hr_tracked_bpm,
                (unsigned int)vital_out->hr_sqi,
                vital_out->hr_snr,
                VitalEstimator_AxisToString(vital_out->hr_axis),
                vital_out->hr_raw_bpm,
                vital_out->hr_raw_snr,
                vital_out->hr_clean_bpm,
                vital_out->hr_clean_snr,
                (unsigned int)vital_out->hr_near_harmonic,
                (unsigned int)vital_out->hr_harmonic_k,
                vital_out->hr_cand_bpm[0], vital_out->hr_cand_snr[0], (unsigned int)vital_out->hr_cand_harm_k[0],
                vital_out->hr_cand_bpm[1], vital_out->hr_cand_snr[1], (unsigned int)vital_out->hr_cand_harm_k[1],
                vital_out->hr_cand_bpm[2], vital_out->hr_cand_snr[2], (unsigned int)vital_out->hr_cand_harm_k[2],
                (unsigned int)vital_out->hr_cand_sqi[0], (unsigned int)vital_out->hr_cand_transition_sqi[0],
                VitalEstimator_AxisToString(vital_out->hr_cand_axis[0]),
                vital_out->hr_cand_prominence[0], vital_out->hr_cand_axis_support[0], vital_out->hr_cand_harmonic_quality[0],
                vital_out->hr_cand_periodicity[0], vital_out->hr_cand_half_consistency[0],
                vital_out->hr_cand_recent_consistency[0], vital_out->hr_cand_peak_shape[0], vital_out->hr_cand_recent_bpm[0],
                (unsigned int)vital_out->hr_cand_sqi[1], (unsigned int)vital_out->hr_cand_transition_sqi[1],
                VitalEstimator_AxisToString(vital_out->hr_cand_axis[1]),
                vital_out->hr_cand_prominence[1], vital_out->hr_cand_axis_support[1], vital_out->hr_cand_harmonic_quality[1],
                vital_out->hr_cand_periodicity[1], vital_out->hr_cand_half_consistency[1],
                vital_out->hr_cand_recent_consistency[1], vital_out->hr_cand_peak_shape[1], vital_out->hr_cand_recent_bpm[1],
                (unsigned int)vital_out->hr_cand_sqi[2], (unsigned int)vital_out->hr_cand_transition_sqi[2],
                VitalEstimator_AxisToString(vital_out->hr_cand_axis[2]),
                vital_out->hr_cand_prominence[2], vital_out->hr_cand_axis_support[2], vital_out->hr_cand_harmonic_quality[2],
                vital_out->hr_cand_periodicity[2], vital_out->hr_cand_half_consistency[2],
                vital_out->hr_cand_recent_consistency[2], vital_out->hr_cand_peak_shape[2], vital_out->hr_cand_recent_bpm[2],
                vital_out->motion_quality,
                VitalEstimator_MotionClassToString(vital_out->motion_class),
                vital_out->rr_candidate_bpm,
                vital_out->rr_prominence,
                vital_out->rr_axis_support,
                vital_out->rr_continuity_quality,
                vital_out->rr_harmonic_quality,
                vital_out->hr_candidate_bpm,
                vital_out->hr_prominence,
                vital_out->hr_axis_support,
                vital_out->hr_continuity_quality,
                vital_out->hr_harmonic_quality,
                vital_out->hr_candidate_margin,
                vital_out->hr_periodicity_quality,
                vital_out->hr_half_consistency,
                vital_out->hr_recent_consistency,
                vital_out->hr_peak_shape_quality,
                vital_out->hr_history_quality,
                vital_out->hr_axis_stability,
                vital_out->hr_output_agreement,
                vital_out->hr_rr_suspicion,
                vital_out->hr_challenger_bpm,
                vital_out->hr_challenger_evidence,
                (unsigned int)vital_out->hr_consensus_used);
    }
    else
    {
        /* 93 empty vital-result fields. The preceding section already wrote
         * the comma after the last non-vital field, so emit 92 more commas. */
        for (unsigned int i = 0U; i < 92U; i++)
        {
            fputc(',', out);
        }
        fputc('\n', out);
    }
}

static void Pipeline_Init(PcPipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));
    RestDetector_Init(&pipeline->rest);
    VitalEstimator_Init(&pipeline->vital);
    pipeline->previous_state = REST_STATE_NOT_REST;
}

static bool Pipeline_ProcessSample(PcPipeline_t *pipeline,
                                   const ImuSample_t *sample,
                                   const CsvReader_t *reader,
                                   FILE *output)
{
    RestDetectorOutput_t rest_out;
    VitalOutput_t vital_out;
    bool rest_update_ok;
    bool entered_measurement = false;
    bool left_measurement = false;
    bool estimate_called = false;
    bool estimate_returned = false;

    memset(&rest_out, 0, sizeof(rest_out));
    rest_out.state = RestDetector_GetState(&pipeline->rest);
    rest_out.local_label = REST_LOCAL_DISTURBANCE;
    memset(&vital_out, 0, sizeof(vital_out));
    vital_out.rr_axis = VITAL_AXIS_NONE;
    vital_out.hr_axis = VITAL_AXIS_NONE;
    vital_out.hr_status = VITAL_HR_STATUS_NONE;

    /* This order deliberately mirrors RestDetectorApp_ProcessSample(). */
    if (!RestDetector_PushSample(&pipeline->rest, sample))
    {
        return false;
    }
    pipeline->sample_count++;

    if (RestDetector_GetState(&pipeline->rest) == REST_STATE_MEASUREMENT)
    {
        VitalEstimator_PushSample(&pipeline->vital, sample);
        pipeline->vital_was_running = 1U;
    }
    else if (pipeline->vital_was_running != 0U)
    {
        VitalEstimator_Reset(&pipeline->vital);
        pipeline->vital_was_running = 0U;
        pipeline->reset_since_last_second = 1U;
    }

    if ((pipeline->sample_count % REST_FS_HZ) != 0U)
    {
        return true;
    }

    pipeline->second_count++;
    rest_update_ok = RestDetector_Update1s(&pipeline->rest, &rest_out);

    if (rest_update_ok)
    {
        entered_measurement = ((rest_out.state == REST_STATE_MEASUREMENT) &&
                               (pipeline->previous_state != REST_STATE_MEASUREMENT));
        left_measurement = ((rest_out.state != REST_STATE_MEASUREMENT) &&
                            (pipeline->previous_state == REST_STATE_MEASUREMENT));

        if (entered_measurement)
        {
            VitalEstimator_Reset(&pipeline->vital);
            pipeline->vital_was_running = 1U;
            pipeline->reset_since_last_second = 1U;
        }

        if (rest_out.state == REST_STATE_MEASUREMENT)
        {
            VitalEstimator_SetMotionContext(&pipeline->vital, &rest_out);
        }

        if ((rest_out.state == REST_STATE_MEASUREMENT) &&
            ((pipeline->second_count % VITAL_ESTIMATE_PERIOD_SEC) == 0U))
        {
            estimate_called = true;
            estimate_returned = VitalEstimator_Estimate(&pipeline->vital, &vital_out);
        }

        pipeline->previous_state = rest_out.state;
    }

    WriteSecondRow(output,
                   pipeline,
                   reader,
                   rest_update_ok,
                   &rest_out,
                   entered_measurement,
                   left_measurement,
                   estimate_called,
                   estimate_returned,
                   &vital_out);

    pipeline->reset_since_last_second = 0U;
    return true;
}

int main(int argc, char **argv)
{
    const char *input_path;
    const char *output_path;
    CsvAccelUnit_t accel_unit = CSV_ACC_UNIT_G;
    CsvGyroUnit_t gyro_unit = CSV_GYRO_UNIT_DPS;
    CsvReader_t reader;
    PcPipeline_t pipeline;
    FILE *output = NULL;
    char error_message[512];
    bool eof = false;
    int exit_code = EXIT_FAILURE;

    if (argc < 3)
    {
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    input_path = argv[1];
    output_path = argv[2];

    for (int i = 3; i < argc; i++)
    {
        if ((strcmp(argv[i], "--acc-unit") == 0) && (i + 1 < argc))
        {
            i++;
            if (strcmp(argv[i], "g") == 0)
                accel_unit = CSV_ACC_UNIT_G;
            else if (strcmp(argv[i], "mps2") == 0)
                accel_unit = CSV_ACC_UNIT_MPS2;
            else
            {
                fprintf(stderr, "Unknown acceleration unit: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        }
        else if ((strcmp(argv[i], "--gyro-unit") == 0) && (i + 1 < argc))
        {
            i++;
            if (strcmp(argv[i], "dps") == 0)
                gyro_unit = CSV_GYRO_UNIT_DPS;
            else if (strcmp(argv[i], "rads") == 0)
                gyro_unit = CSV_GYRO_UNIT_RAD_S;
            else
            {
                fprintf(stderr, "Unknown gyroscope unit: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (!CsvReader_Open(&reader, input_path, accel_unit, gyro_unit,
                        error_message, sizeof(error_message)))
    {
        fprintf(stderr, "CSV error: %s\n", error_message);
        return EXIT_FAILURE;
    }

    output = fopen(output_path, "wb");
    if (output == NULL)
    {
        fprintf(stderr, "Cannot create output file: %s\n", output_path);
        CsvReader_Close(&reader);
        return EXIT_FAILURE;
    }

    WriteHeader(output);
    Pipeline_Init(&pipeline);

    while (!eof)
    {
        ImuSample_t sample;
        if (!CsvReader_ReadSample(&reader, &sample, &eof,
                                  error_message, sizeof(error_message)))
        {
            fprintf(stderr, "CSV error: %s\n", error_message);
            goto cleanup;
        }

        if (!eof)
        {
            if (!Pipeline_ProcessSample(&pipeline, &sample, &reader, output))
            {
                fprintf(stderr, "Pipeline error at algorithm sample %llu\n",
                        (unsigned long long)pipeline.sample_count);
                goto cleanup;
            }
        }
    }

    if (pipeline.sample_count == 0U)
    {
        fprintf(stderr, "Input CSV contains no data rows.\n");
        goto cleanup;
    }

    if ((pipeline.sample_count % REST_FS_HZ) != 0U)
    {
        /* A partial final second is expected when capture duration is not an
         * exact multiple of REST_FS_HZ. It is informational, not an error.
         */
        fprintf(stdout,
                "Info: final partial second contains %llu sample(s); no 1-second output row was written for it.\n",
                (unsigned long long)(pipeline.sample_count % REST_FS_HZ));
    }

    if (reader.has_time)
    {
        const double duration_s = reader.current_time_s - reader.first_time_s;
        if ((duration_s > 0.0) && (pipeline.sample_count > 1U))
        {
            const double observed_fs = (double)(pipeline.sample_count - 1U) / duration_s;
            const double expected_fs = (double)REST_FS_HZ;
            const double relative_error = fabs(observed_fs - expected_fs) / expected_fs;

            fprintf(stdout, "Observed average input rate: %.6f Hz\n", observed_fs);
            if (relative_error > 0.01)
            {
                fprintf(stderr,
                        "Warning: input is not within 1%% of %.3f Hz. "
                        "This baseline does not resample; frequency and window timing will be wrong.\n",
                        expected_fs);
            }
        }

        if ((reader.timing_gap_count > 0U) || (reader.nonmonotonic_time_count > 0U))
        {
            fprintf(stderr,
                    "Warning: timestamp diagnostics found %llu irregular interval(s) and %llu non-monotonic interval(s).\n",
                    (unsigned long long)reader.timing_gap_count,
                    (unsigned long long)reader.nonmonotonic_time_count);
        }
    }

    fprintf(stdout,
            "Completed: %llu samples, %llu whole algorithm seconds. Output: %s\n",
            (unsigned long long)pipeline.sample_count,
            (unsigned long long)pipeline.second_count,
            output_path);
    exit_code = EXIT_SUCCESS;

cleanup:
    if (output != NULL)
    {
        fclose(output);
    }
    CsvReader_Close(&reader);
    return exit_code;
}
