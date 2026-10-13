#include "ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_status_without_temperature(void) {
    char status[256];
    size_t length = format_status_line(
        status,
        sizeof(status),
        120,
        12.3,
        45.6,
        0,
        0.0,
        "12:34:56"
    );

    assert(length == strlen(status));
    assert(strstr(status, "CPU [#---------]  12%") != NULL);
    assert(strstr(status, "RAM [#####-----]  46%") != NULL);
    assert(strstr(status, "TEMP") == NULL);
    assert(strstr(status, "GiB") == NULL);
}

static void test_status_with_temperature_at_each_width(void) {
    char status[256];

    format_status_line(status, sizeof(status), 120, 12.3, 45.6, 1, 72.6, "12:34:56");
    assert(strstr(status, "TEMP 73C") != NULL);

    format_status_line(status, sizeof(status), 60, 12.3, 45.6, 1, 72.6, "12:34:56");
    assert(strcmp(status, " CPU  12% | RAM  46% | TEMP 73C | 12:34:56 ") == 0);

    format_status_line(status, sizeof(status), 46, 12.3, 45.6, 1, 72.6, "12:34:56");
    assert(strcmp(status, " CPU 12% | RAM 46% | TEMP 73C | 12:34:56 ") == 0);

    format_status_line(status, sizeof(status), 40, 12.3, 45.6, 1, 72.6, "12:34:56");
    assert(strcmp(status, " C12% M46% T73C 12:34 ") == 0);

    format_status_line(status, sizeof(status), 24, 12.3, 45.6, 1, 72.6, "12:34:56");
    assert(strcmp(status, " C12% M46% 12:34 ") == 0);
}

static void test_temperature_changes_fire_style(void) {
    FireStyle cool = fire_style_from_temperature(1, 35.0, 100.0);
    FireStyle warm = fire_style_from_temperature(1, 70.0, 100.0);
    FireStyle hot = fire_style_from_temperature(1, 100.0, 100.0);
    FireStyle fallback = fire_style_from_temperature(0, 0.0, 100.0);

    assert(cool.flame_red_gain < warm.flame_red_gain);
    assert(warm.flame_red_gain < hot.flame_red_gain);
    assert(cool.flame_green_gain < warm.flame_green_gain);
    assert(warm.flame_green_gain < hot.flame_green_gain);
    assert(cool.flame_blue_gain < warm.flame_blue_gain);
    assert(warm.flame_blue_gain < hot.flame_blue_gain);

    assert(cool.scene_red_gain == warm.scene_red_gain);
    assert(warm.scene_red_gain == hot.scene_red_gain);
    assert(cool.scene_green_gain == warm.scene_green_gain);
    assert(warm.scene_green_gain == hot.scene_green_gain);
    assert(cool.scene_blue_gain == warm.scene_blue_gain);
    assert(warm.scene_blue_gain == hot.scene_blue_gain);

    assert(fallback.flame_red_gain > cool.flame_red_gain);
    assert(fallback.flame_red_gain < hot.flame_red_gain);
    assert(fallback.scene_red_gain == hot.scene_red_gain);

    FireStyle fallback_critical = fire_style_from_temperature(1, 70.0, 0.0);
    assert(fallback_critical.flame_red_gain == warm.flame_red_gain);
    assert(fallback_critical.flame_green_gain == warm.flame_green_gain);
    assert(fallback_critical.flame_blue_gain == warm.flame_blue_gain);
}

int main(void) {
    test_status_without_temperature();
    test_status_with_temperature_at_each_width();
    test_temperature_changes_fire_style();
    puts("ui tests: OK");
    return 0;
}
