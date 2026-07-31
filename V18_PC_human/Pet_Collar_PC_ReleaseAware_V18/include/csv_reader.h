#ifndef CSV_READER_H
#define CSV_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rest_detector.h"

typedef enum
{
    CSV_ACC_UNIT_G = 0,
    CSV_ACC_UNIT_MPS2
} CsvAccelUnit_t;

typedef enum
{
    CSV_GYRO_UNIT_DPS = 0,
    CSV_GYRO_UNIT_RAD_S
} CsvGyroUnit_t;

typedef struct
{
    FILE *fp;
    char delimiter;
    uint32_t line_number;

    int column_ax;
    int column_ay;
    int column_az;
    int column_gx;
    int column_gy;
    int column_gz;
    int column_time;
    int column_sample_index;
    int column_count;

    CsvAccelUnit_t accel_unit;
    CsvGyroUnit_t gyro_unit;

    bool has_time;
    bool has_sample_index;
    bool first_time_seen;
    double first_time_s;
    double previous_time_s;
    double current_time_s;
    uint64_t current_input_index;
    uint64_t timing_gap_count;
    uint64_t nonmonotonic_time_count;
} CsvReader_t;

bool CsvReader_Open(CsvReader_t *reader,
                    const char *path,
                    CsvAccelUnit_t accel_unit,
                    CsvGyroUnit_t gyro_unit,
                    char *error_message,
                    size_t error_message_size);

bool CsvReader_ReadSample(CsvReader_t *reader,
                          ImuSample_t *sample,
                          bool *end_of_file,
                          char *error_message,
                          size_t error_message_size);

void CsvReader_Close(CsvReader_t *reader);

#endif /* CSV_READER_H */
