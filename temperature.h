#ifndef FIREC_TEMPERATURE_H
#define FIREC_TEMPERATURE_H

#include <stddef.h>

#ifndef FIREC_PATH_MAX
#define FIREC_PATH_MAX 4096
#endif

#define FIREC_MAX_TEMPERATURE_SENSORS 32

typedef struct {
    char paths[FIREC_MAX_TEMPERATURE_SENSORS][FIREC_PATH_MAX];
    size_t count;
    double critical_celsius;
} TemperatureSource;

/*
 * Discover the most representative CPU temperature source exposed through
 * Linux hwmon. Package-level sensors are preferred. If only per-core sensors
 * are available, all matching cores are retained and the hottest value is
 * reported at runtime.
 */
int temperature_source_discover(TemperatureSource *source, const char *hwmon_root);

/* Read the hottest valid value from the selected source. */
int temperature_source_read(const TemperatureSource *source, double *temperature_celsius);

#endif
