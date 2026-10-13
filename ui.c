#include "ui.h"

#include <stdio.h>
#include <string.h>

static void make_bar(char *bar, int length, double percent) {
    int filled = (int)(percent * length / 100.0 + 0.5);
    if (filled < 0) filled = 0;
    if (filled > length) filled = length;

    for (int i = 0; i < length; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[length] = '\0';
}

size_t format_status_line(
    char *status,
    size_t status_size,
    int width,
    double cpu_percent,
    double memory_percent,
    int temperature_available,
    double temperature_celsius,
    const char *current_time
) {
    if (status == NULL || status_size == 0 || current_time == NULL) {
        return 0;
    }

    if (width >= 96) {
        char cpu_bar[11];
        char memory_bar[11];
        make_bar(cpu_bar, 10, cpu_percent);
        make_bar(memory_bar, 10, memory_percent);
        if (temperature_available) {
            snprintf(
                status,
                status_size,
                " CPU [%s] %3.0f%% | RAM [%s] %3.0f%% | TEMP %.0fC | TIME %s ",
                cpu_bar,
                cpu_percent,
                memory_bar,
                memory_percent,
                temperature_celsius,
                current_time
            );
        } else {
            snprintf(
                status,
                status_size,
                " CPU [%s] %3.0f%% | RAM [%s] %3.0f%% | TIME %s ",
                cpu_bar,
                cpu_percent,
                memory_bar,
                memory_percent,
                current_time
            );
        }
    } else if (width >= 60) {
        if (temperature_available) {
            snprintf(
                status,
                status_size,
                " CPU %3.0f%% | RAM %3.0f%% | TEMP %.0fC | %s ",
                cpu_percent,
                memory_percent,
                temperature_celsius,
                current_time
            );
        } else {
            snprintf(
                status,
                status_size,
                " CPU %3.0f%% | RAM %3.0f%% | %s ",
                cpu_percent,
                memory_percent,
                current_time
            );
        }
    } else if (width >= 46 && temperature_available) {
        snprintf(
            status,
            status_size,
            " CPU %.0f%% | RAM %.0f%% | TEMP %.0fC | %s ",
            cpu_percent,
            memory_percent,
            temperature_celsius,
            current_time
        );
    } else if (width >= 36 && !temperature_available) {
        snprintf(
            status,
            status_size,
            " CPU %.0f%% | RAM %.0f%% | %s ",
            cpu_percent,
            memory_percent,
            current_time
        );
    } else if (width >= 25 && temperature_available) {
        snprintf(
            status,
            status_size,
            " C%.0f%% M%.0f%% T%.0fC %.5s ",
            cpu_percent,
            memory_percent,
            temperature_celsius,
            current_time
        );
    } else {
        snprintf(
            status,
            status_size,
            " C%.0f%% M%.0f%% %.5s ",
            cpu_percent,
            memory_percent,
            current_time
        );
    }

    return strlen(status);
}

FireStyle fire_style_from_temperature(
    int temperature_available,
    double temperature_celsius,
    double critical_celsius
) {
    /* The former 100 C rendering is the artistic high point. Keep the scene
     * exposure close to it, while reserving the final brightness step for the
     * temperature-reactive flame. */
    const double hot_red_gain = 332.0;
    const double hot_green_gain = 314.0;
    const double hot_blue_gain = 284.0;
    const double scene_exposure = 0.90;

    FireStyle style = {
        .scene_red_gain = (int)(hot_red_gain * scene_exposure + 0.5),
        .scene_green_gain = (int)(hot_green_gain * scene_exposure + 0.5),
        .scene_blue_gain = (int)(hot_blue_gain * scene_exposure + 0.5),
    };

    if (critical_celsius < 70.0 || critical_celsius > 130.0) {
        critical_celsius = 100.0;
    }
    double cool_celsius = critical_celsius - 60.0;
    if (cool_celsius < 35.0) cool_celsius = 35.0;
    if (cool_celsius > 55.0) cool_celsius = 55.0;

    double heat = 0.55;
    if (temperature_available) {
        heat = (temperature_celsius - cool_celsius)
            / (critical_celsius - cool_celsius);
        if (heat < 0.0) heat = 0.0;
        if (heat > 1.0) heat = 1.0;
        heat = heat * heat * (3.0 - 2.0 * heat);
    }

    /*
     * Low temperatures retain a warm red flame. As heat rises, green and blue
     * catch up faster, producing a brighter yellow-white core. Frame rate is
     * intentionally constant to avoid a load feedback loop.
     */
    style.flame_red_gain = (int)(hot_red_gain * (0.90 + 0.10 * heat) + 0.5);
    style.flame_green_gain = (int)(hot_green_gain * (0.84 + 0.16 * heat) + 0.5);
    style.flame_blue_gain = (int)(hot_blue_gain * (0.78 + 0.22 * heat) + 0.5);
    return style;
}
