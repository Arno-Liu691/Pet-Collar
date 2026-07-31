#include "csv_reader.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CSV_MAX_LINE_LENGTH 8192
#define CSV_MAX_COLUMNS 256
#define CSV_G0_MPS2 9.80665
#define CSV_RAD_TO_DEG 57.2957795130823208768
#define CSV_EXPECTED_FS_HZ 100.0
#define CSV_EXPECTED_DT_S (1.0 / CSV_EXPECTED_FS_HZ)

static void Csv_SetError(char *buffer, size_t size, const char *message)
{
    if ((buffer != NULL) && (size > 0U))
    {
        (void)snprintf(buffer, size, "%s", message);
    }
}

static void Csv_SetErrorLine(char *buffer,
                             size_t size,
                             uint32_t line_number,
                             const char *message)
{
    if ((buffer != NULL) && (size > 0U))
    {
        (void)snprintf(buffer, size, "line %lu: %s",
                       (unsigned long)line_number, message);
    }
}

static void Csv_Trim(char *text)
{
    char *start;
    char *end;

    if (text == NULL)
    {
        return;
    }

    start = text;
    while ((*start != '\0') && isspace((unsigned char)*start))
    {
        start++;
    }

    if (start != text)
    {
        memmove(text, start, strlen(start) + 1U);
    }

    end = text + strlen(text);
    while ((end > text) && isspace((unsigned char)end[-1]))
    {
        end--;
    }
    *end = '\0';

    if ((text[0] == '"') && (end > text + 1) && (end[-1] == '"'))
    {
        memmove(text, text + 1, strlen(text));
        end = text + strlen(text);
        if ((end > text) && (end[-1] == '"'))
        {
            end[-1] = '\0';
        }
    }
}

static void Csv_NormalizeHeader(const char *input, char *output, size_t output_size)
{
    size_t j = 0U;

    if ((output == NULL) || (output_size == 0U))
    {
        return;
    }

    if (input != NULL)
    {
        for (size_t i = 0U; input[i] != '\0'; i++)
        {
            unsigned char c = (unsigned char)input[i];
            if (isalnum(c))
            {
                if (j + 1U < output_size)
                {
                    output[j++] = (char)tolower(c);
                }
            }
        }
    }

    output[j] = '\0';
}

static bool Csv_HeaderMatches(const char *normalised,
                              const char *const *aliases,
                              size_t alias_count)
{
    for (size_t i = 0U; i < alias_count; i++)
    {
        if (strcmp(normalised, aliases[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

static char Csv_DetectDelimiter(const char *line)
{
    const char candidates[] = { ',', ';', '\t' };
    size_t best_count = 0U;
    char best = ',';

    for (size_t c = 0U; c < sizeof(candidates); c++)
    {
        size_t count = 0U;
        bool in_quotes = false;
        for (size_t i = 0U; line[i] != '\0'; i++)
        {
            if (line[i] == '"')
            {
                in_quotes = !in_quotes;
            }
            else if ((!in_quotes) && (line[i] == candidates[c]))
            {
                count++;
            }
        }
        if (count > best_count)
        {
            best_count = count;
            best = candidates[c];
        }
    }

    return best;
}

static int Csv_SplitLine(char *line, char delimiter, char **fields, int max_fields)
{
    int count = 0;
    char *read_ptr = line;
    char *field_start = line;
    bool in_quotes = false;

    if ((line == NULL) || (fields == NULL) || (max_fields <= 0))
    {
        return 0;
    }

    while (true)
    {
        char c = *read_ptr;

        if (c == '"')
        {
            in_quotes = !in_quotes;
        }

        if (((c == delimiter) && (!in_quotes)) || (c == '\0') ||
            (c == '\r') || (c == '\n'))
        {
            if (count >= max_fields)
            {
                return -1;
            }

            *read_ptr = '\0';
            fields[count++] = field_start;
            Csv_Trim(field_start);

            if ((c == '\0') || (c == '\r') || (c == '\n'))
            {
                break;
            }

            field_start = read_ptr + 1;
        }

        read_ptr++;
    }

    return count;
}

static bool Csv_ParseDouble(const char *text, double *value)
{
    char *end_ptr;
    double parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0'))
    {
        return false;
    }

    errno = 0;
    end_ptr = NULL;
    parsed = strtod(text, &end_ptr);

    if ((errno != 0) || (end_ptr == text))
    {
        return false;
    }

    while ((*end_ptr != '\0') && isspace((unsigned char)*end_ptr))
    {
        end_ptr++;
    }

    if ((*end_ptr != '\0') || (!isfinite(parsed)))
    {
        return false;
    }

    *value = parsed;
    return true;
}

static bool Csv_IsBlankOrComment(const char *line)
{
    const char *p = line;
    while ((*p != '\0') && isspace((unsigned char)*p))
    {
        p++;
    }
    return ((*p == '\0') || (*p == '#'));
}

static bool Csv_MapHeader(CsvReader_t *reader,
                          char *header_line,
                          char *error_message,
                          size_t error_message_size)
{
    char *fields[CSV_MAX_COLUMNS];
    int count;

    static const char *const ax_aliases[] = {
        "ax", "accx", "accelx", "accelerationx", "accelerometerx", "xacc"
    };
    static const char *const ay_aliases[] = {
        "ay", "accy", "accely", "accelerationy", "accelerometery", "yacc"
    };
    static const char *const az_aliases[] = {
        "az", "accz", "accelz", "accelerationz", "accelerometerz", "zacc"
    };
    static const char *const gx_aliases[] = {
        "gx", "gyrox", "gyrx", "angularvelocityx", "xgyro"
    };
    static const char *const gy_aliases[] = {
        "gy", "gyroy", "gyry", "angularvelocityy", "ygyro"
    };
    static const char *const gz_aliases[] = {
        "gz", "gyroz", "gyrz", "angularvelocityz", "zgyro"
    };
    static const char *const time_aliases[] = {
        "time", "times", "timestamp", "timestamps", "elapsedtime", "elapsedtimes", "seconds"
    };
    static const char *const index_aliases[] = {
        "sample", "sampleindex", "sampleid", "index", "row", "sequence"
    };

    reader->delimiter = Csv_DetectDelimiter(header_line);
    count = Csv_SplitLine(header_line, reader->delimiter, fields, CSV_MAX_COLUMNS);
    if (count <= 0)
    {
        Csv_SetError(error_message, error_message_size, "could not parse CSV header");
        return false;
    }

    reader->column_count = count;
    reader->column_ax = -1;
    reader->column_ay = -1;
    reader->column_az = -1;
    reader->column_gx = -1;
    reader->column_gy = -1;
    reader->column_gz = -1;
    reader->column_time = -1;
    reader->column_sample_index = -1;

    for (int i = 0; i < count; i++)
    {
        char normalised[128];
        Csv_NormalizeHeader(fields[i], normalised, sizeof(normalised));

        if (Csv_HeaderMatches(normalised, ax_aliases, sizeof(ax_aliases) / sizeof(ax_aliases[0])))
            reader->column_ax = i;
        else if (Csv_HeaderMatches(normalised, ay_aliases, sizeof(ay_aliases) / sizeof(ay_aliases[0])))
            reader->column_ay = i;
        else if (Csv_HeaderMatches(normalised, az_aliases, sizeof(az_aliases) / sizeof(az_aliases[0])))
            reader->column_az = i;
        else if (Csv_HeaderMatches(normalised, gx_aliases, sizeof(gx_aliases) / sizeof(gx_aliases[0])))
            reader->column_gx = i;
        else if (Csv_HeaderMatches(normalised, gy_aliases, sizeof(gy_aliases) / sizeof(gy_aliases[0])))
            reader->column_gy = i;
        else if (Csv_HeaderMatches(normalised, gz_aliases, sizeof(gz_aliases) / sizeof(gz_aliases[0])))
            reader->column_gz = i;
        else if (Csv_HeaderMatches(normalised, time_aliases, sizeof(time_aliases) / sizeof(time_aliases[0])))
            reader->column_time = i;
        else if (Csv_HeaderMatches(normalised, index_aliases, sizeof(index_aliases) / sizeof(index_aliases[0])))
            reader->column_sample_index = i;
    }

    if ((reader->column_ax < 0) || (reader->column_ay < 0) ||
        (reader->column_az < 0) || (reader->column_gx < 0) ||
        (reader->column_gy < 0) || (reader->column_gz < 0))
    {
        Csv_SetError(error_message, error_message_size,
                     "required columns not found; use headers ax,ay,az,gx,gy,gz (aliases are also accepted)");
        return false;
    }

    reader->has_time = (reader->column_time >= 0);
    reader->has_sample_index = (reader->column_sample_index >= 0);
    return true;
}

bool CsvReader_Open(CsvReader_t *reader,
                    const char *path,
                    CsvAccelUnit_t accel_unit,
                    CsvGyroUnit_t gyro_unit,
                    char *error_message,
                    size_t error_message_size)
{
    char line[CSV_MAX_LINE_LENGTH];

    if ((reader == NULL) || (path == NULL))
    {
        Csv_SetError(error_message, error_message_size, "invalid CsvReader_Open arguments");
        return false;
    }

    memset(reader, 0, sizeof(*reader));
    reader->accel_unit = accel_unit;
    reader->gyro_unit = gyro_unit;

    reader->fp = fopen(path, "rb");
    if (reader->fp == NULL)
    {
        if ((error_message != NULL) && (error_message_size > 0U))
        {
            (void)snprintf(error_message, error_message_size,
                           "cannot open input file: %s", path);
        }
        return false;
    }

    while (fgets(line, sizeof(line), reader->fp) != NULL)
    {
        reader->line_number++;
        if (!Csv_IsBlankOrComment(line))
        {
            if (!Csv_MapHeader(reader, line, error_message, error_message_size))
            {
                CsvReader_Close(reader);
                return false;
            }
            return true;
        }
    }

    Csv_SetError(error_message, error_message_size, "input CSV is empty");
    CsvReader_Close(reader);
    return false;
}

bool CsvReader_ReadSample(CsvReader_t *reader,
                          ImuSample_t *sample,
                          bool *end_of_file,
                          char *error_message,
                          size_t error_message_size)
{
    char line[CSV_MAX_LINE_LENGTH];
    char *fields[CSV_MAX_COLUMNS];

    if (end_of_file != NULL)
    {
        *end_of_file = false;
    }

    if ((reader == NULL) || (reader->fp == NULL) || (sample == NULL))
    {
        Csv_SetError(error_message, error_message_size, "invalid CsvReader_ReadSample arguments");
        return false;
    }

    while (fgets(line, sizeof(line), reader->fp) != NULL)
    {
        int count;
        double values[6];
        const int columns[6] = {
            reader->column_ax, reader->column_ay, reader->column_az,
            reader->column_gx, reader->column_gy, reader->column_gz
        };

        reader->line_number++;
        if (Csv_IsBlankOrComment(line))
        {
            continue;
        }

        count = Csv_SplitLine(line, reader->delimiter, fields, CSV_MAX_COLUMNS);
        if (count < reader->column_count)
        {
            Csv_SetErrorLine(error_message, error_message_size, reader->line_number,
                             "fewer columns than the header");
            return false;
        }

        for (int i = 0; i < 6; i++)
        {
            if ((columns[i] < 0) || (columns[i] >= count) ||
                (!Csv_ParseDouble(fields[columns[i]], &values[i])))
            {
                Csv_SetErrorLine(error_message, error_message_size, reader->line_number,
                                 "invalid or missing six-axis numeric value");
                return false;
            }
        }

        if (reader->accel_unit == CSV_ACC_UNIT_MPS2)
        {
            values[0] /= CSV_G0_MPS2;
            values[1] /= CSV_G0_MPS2;
            values[2] /= CSV_G0_MPS2;
        }

        if (reader->gyro_unit == CSV_GYRO_UNIT_RAD_S)
        {
            values[3] *= CSV_RAD_TO_DEG;
            values[4] *= CSV_RAD_TO_DEG;
            values[5] *= CSV_RAD_TO_DEG;
        }

        sample->ax = (float)values[0];
        sample->ay = (float)values[1];
        sample->az = (float)values[2];
        sample->gx = (float)values[3];
        sample->gy = (float)values[4];
        sample->gz = (float)values[5];

        reader->current_input_index++;
        if (reader->has_sample_index)
        {
            double index_value;
            if ((reader->column_sample_index >= count) ||
                (!Csv_ParseDouble(fields[reader->column_sample_index], &index_value)) ||
                (index_value < 0.0))
            {
                Csv_SetErrorLine(error_message, error_message_size, reader->line_number,
                                 "invalid sample index");
                return false;
            }
            reader->current_input_index = (uint64_t)(index_value + 0.5);
        }

        if (reader->has_time)
        {
            double time_s;
            if ((reader->column_time >= count) ||
                (!Csv_ParseDouble(fields[reader->column_time], &time_s)))
            {
                Csv_SetErrorLine(error_message, error_message_size, reader->line_number,
                                 "invalid timestamp");
                return false;
            }

            reader->current_time_s = time_s;
            if (!reader->first_time_seen)
            {
                reader->first_time_s = time_s;
                reader->previous_time_s = time_s;
                reader->first_time_seen = true;
            }
            else
            {
                double dt = time_s - reader->previous_time_s;
                if (dt <= 0.0)
                {
                    reader->nonmonotonic_time_count++;
                }
                else if ((dt < 0.5 * CSV_EXPECTED_DT_S) ||
                         (dt > 1.5 * CSV_EXPECTED_DT_S))
                {
                    reader->timing_gap_count++;
                }
                reader->previous_time_s = time_s;
            }
        }

        return true;
    }

    if (ferror(reader->fp))
    {
        Csv_SetError(error_message, error_message_size, "error while reading input CSV");
        return false;
    }

    if (end_of_file != NULL)
    {
        *end_of_file = true;
    }
    return true;
}

void CsvReader_Close(CsvReader_t *reader)
{
    if (reader == NULL)
    {
        return;
    }

    if (reader->fp != NULL)
    {
        fclose(reader->fp);
        reader->fp = NULL;
    }
}
