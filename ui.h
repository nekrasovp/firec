#ifndef FIREC_UI_H
#define FIREC_UI_H

#include <stddef.h>

typedef struct {
    int flame_red_gain;
    int flame_green_gain;
    int flame_blue_gain;
    int scene_red_gain;
    int scene_green_gain;
    int scene_blue_gain;
} FireStyle;

size_t format_status_line(
    char *status,
    size_t status_size,
    int width,
    double cpu_percent,
    double memory_percent,
    int temperature_available,
    double temperature_celsius,
    const char *current_time
);

FireStyle fire_style_from_temperature(
    int temperature_available,
    double temperature_celsius,
    double critical_celsius
);

#endif
