#define _POSIX_C_SOURCE 200809L

#include "temperature.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CANDIDATES 128
#define DEFAULT_CRITICAL_CELSIUS 100.0

typedef struct {
    char path[FIREC_PATH_MAX];
    int score;
    double critical_celsius;
} TemperatureCandidate;

static int read_line(const char *path, char *value, size_t value_size) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    int ok = fgets(value, (int)value_size, file) != NULL;
    fclose(file);
    if (!ok) {
        return 0;
    }

    value[strcspn(value, "\r\n")] = '\0';
    return 1;
}

static int read_millidegrees(const char *path, double *value_celsius) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    long long value = 0;
    int ok = fscanf(file, "%lld", &value) == 1;
    fclose(file);
    if (!ok || value < 1000 || value > 150000) {
        return 0;
    }

    *value_celsius = value / 1000.0;
    return 1;
}

static int sensor_has_fault(const char *input_path) {
    size_t length = strlen(input_path);
    static const char input_suffix[] = "_input";
    if (length < sizeof(input_suffix) - 1
        || strcmp(input_path + length - (sizeof(input_suffix) - 1), input_suffix) != 0) {
        return 0;
    }

    char fault_path[FIREC_PATH_MAX];
    if (length - (sizeof(input_suffix) - 1) + sizeof("_fault") > sizeof(fault_path)) {
        return 0;
    }
    memcpy(fault_path, input_path, length - (sizeof(input_suffix) - 1));
    strcpy(fault_path + length - (sizeof(input_suffix) - 1), "_fault");

    FILE *file = fopen(fault_path, "r");
    if (file == NULL) {
        return 0;
    }

    int fault = 0;
    int parsed = fscanf(file, "%d", &fault) == 1;
    fclose(file);
    return parsed && fault != 0;
}

static int contains_case_insensitive(const char *value, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0) {
        return 1;
    }

    for (; *value != '\0'; value++) {
        size_t i = 0;
        while (i < needle_length
               && value[i] != '\0'
               && tolower((unsigned char)value[i])
                   == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_length) {
            return 1;
        }
    }
    return 0;
}

static int is_cpu_chip(const char *chip) {
    static const char *const cpu_chips[] = {
        "coretemp",
        "k10temp",
        "zenpower",
        "peci_cputemp",
        "cpu_thermal",
        "soc_thermal",
    };

    for (size_t i = 0; i < sizeof(cpu_chips) / sizeof(cpu_chips[0]); i++) {
        if (strcmp(chip, cpu_chips[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int sensor_score(const char *chip, const char *label) {
    if (contains_case_insensitive(label, "package id")) return 1000;
    if (contains_case_insensitive(label, "tctl")) return 980;
    if (contains_case_insensitive(label, "tdie")) return 970;
    if (contains_case_insensitive(label, "cpu")) return 950;
    if (contains_case_insensitive(label, "die")) return 900;
    if (is_cpu_chip(chip) && contains_case_insensitive(label, "core")) return 700;
    if (is_cpu_chip(chip) && label[0] == '\0') return 650;
    return 0;
}

static int is_temperature_input(const char *name, int *index) {
    static const char prefix[] = "temp";
    static const char suffix[] = "_input";
    if (strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
        return 0;
    }

    char *end = NULL;
    long parsed_index = strtol(name + sizeof(prefix) - 1, &end, 10);
    if (end == name + sizeof(prefix) - 1
        || strcmp(end, suffix) != 0
        || parsed_index <= 0
        || parsed_index > INT_MAX) {
        return 0;
    }

    *index = (int)parsed_index;
    return 1;
}

static int make_path(
    char *destination,
    size_t destination_size,
    const char *directory,
    const char *name
) {
    int length = snprintf(destination, destination_size, "%s/%s", directory, name);
    return length > 0 && (size_t)length < destination_size;
}

static double read_critical_temperature(const char *directory, int index) {
    char name[64];
    char path[FIREC_PATH_MAX];
    snprintf(name, sizeof(name), "temp%d_crit", index);
    if (!make_path(path, sizeof(path), directory, name)) {
        return 0.0;
    }

    double critical = 0.0;
    return read_millidegrees(path, &critical) ? critical : 0.0;
}

static size_t scan_candidates(
    const char *hwmon_root,
    TemperatureCandidate *candidates,
    size_t capacity
) {
    DIR *root = opendir(hwmon_root);
    if (root == NULL) {
        return 0;
    }

    size_t count = 0;
    struct dirent *hwmon_entry;
    while ((hwmon_entry = readdir(root)) != NULL && count < capacity) {
        if (strncmp(hwmon_entry->d_name, "hwmon", 5) != 0) {
            continue;
        }

        char directory[FIREC_PATH_MAX];
        if (!make_path(directory, sizeof(directory), hwmon_root, hwmon_entry->d_name)) {
            continue;
        }

        char name_path[FIREC_PATH_MAX];
        char chip[64] = "";
        if (!make_path(name_path, sizeof(name_path), directory, "name")
            || !read_line(name_path, chip, sizeof(chip))) {
            continue;
        }

        DIR *device = opendir(directory);
        if (device == NULL) {
            continue;
        }

        struct dirent *sensor_entry;
        while ((sensor_entry = readdir(device)) != NULL && count < capacity) {
            int index = 0;
            if (!is_temperature_input(sensor_entry->d_name, &index)) {
                continue;
            }

            char input_path[FIREC_PATH_MAX];
            if (!make_path(
                    input_path,
                    sizeof(input_path),
                    directory,
                    sensor_entry->d_name
                )) {
                continue;
            }

            double current_temperature = 0.0;
            if (sensor_has_fault(input_path)
                || !read_millidegrees(input_path, &current_temperature)) {
                continue;
            }

            char label_name[64];
            char label_path[FIREC_PATH_MAX];
            char label[128] = "";
            snprintf(label_name, sizeof(label_name), "temp%d_label", index);
            if (make_path(label_path, sizeof(label_path), directory, label_name)) {
                (void)read_line(label_path, label, sizeof(label));
            }

            int score = sensor_score(chip, label);
            if (score == 0) {
                continue;
            }

            TemperatureCandidate *candidate = &candidates[count++];
            snprintf(candidate->path, sizeof(candidate->path), "%s", input_path);
            candidate->score = score;
            candidate->critical_celsius = read_critical_temperature(directory, index);
        }
        closedir(device);
    }
    closedir(root);
    return count;
}

int temperature_source_discover(TemperatureSource *source, const char *hwmon_root) {
    if (source == NULL || hwmon_root == NULL) {
        return 0;
    }

    memset(source, 0, sizeof(*source));
    source->critical_celsius = DEFAULT_CRITICAL_CELSIUS;

    TemperatureCandidate candidates[MAX_CANDIDATES];
    size_t candidate_count = scan_candidates(hwmon_root, candidates, MAX_CANDIDATES);
    if (candidate_count == 0) {
        return 0;
    }

    int best_score = 0;
    for (size_t i = 0; i < candidate_count; i++) {
        if (candidates[i].score > best_score) {
            best_score = candidates[i].score;
        }
    }

    double lowest_critical = 0.0;
    for (size_t i = 0;
         i < candidate_count && source->count < FIREC_MAX_TEMPERATURE_SENSORS;
         i++) {
        if (candidates[i].score != best_score) {
            continue;
        }

        size_t path_length = strlen(candidates[i].path);
        if (path_length >= sizeof(source->paths[source->count])) {
            continue;
        }
        memcpy(
            source->paths[source->count],
            candidates[i].path,
            path_length + 1
        );
        source->count++;

        double critical = candidates[i].critical_celsius;
        if (critical > 0.0 && (lowest_critical == 0.0 || critical < lowest_critical)) {
            lowest_critical = critical;
        }
    }

    if (lowest_critical > 0.0) {
        source->critical_celsius = lowest_critical;
    }
    return source->count > 0;
}

int temperature_source_read(const TemperatureSource *source, double *temperature_celsius) {
    if (source == NULL || temperature_celsius == NULL || source->count == 0) {
        return 0;
    }

    int found = 0;
    double hottest = 0.0;
    for (size_t i = 0; i < source->count; i++) {
        double current = 0.0;
        if (!sensor_has_fault(source->paths[i])
            && read_millidegrees(source->paths[i], &current)
            && (!found || current > hottest)) {
            hottest = current;
            found = 1;
        }
    }

    if (found) {
        *temperature_celsius = hottest;
    }
    return found;
}
