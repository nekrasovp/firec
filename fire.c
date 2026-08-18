#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "fireplace_frames.h"

static const char ASCII_RAMP[] = " .,:;irsXA253hMHGS#9B&@";
static volatile sig_atomic_t keep_running = 1;

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} CpuSample;

static void stop_animation(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static int read_cpu_sample(CpuSample *sample) {
    FILE *stat = fopen("/proc/stat", "r");
    if (stat == NULL) {
        return 0;
    }

    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;

    int fields = fscanf(
        stat,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &user,
        &nice,
        &system,
        &idle,
        &iowait,
        &irq,
        &softirq,
        &steal
    );
    fclose(stat);

    if (fields < 4) {
        return 0;
    }

    sample->idle = idle + iowait;
    sample->total = user + nice + system + idle + iowait + irq + softirq + steal;
    return 1;
}

static int read_memory(double *used_gib, double *total_gib, double *used_percent) {
    FILE *meminfo = fopen("/proc/meminfo", "r");
    if (meminfo == NULL) {
        return 0;
    }

    unsigned long long total_kib = 0;
    unsigned long long available_kib = 0;
    char line[256];

    while (fgets(line, sizeof(line), meminfo) != NULL) {
        if (sscanf(line, "MemTotal: %llu kB", &total_kib) == 1) {
            continue;
        }
        if (sscanf(line, "MemAvailable: %llu kB", &available_kib) == 1) {
            continue;
        }
    }
    fclose(meminfo);

    if (total_kib == 0 || available_kib > total_kib) {
        return 0;
    }

    unsigned long long used_kib = total_kib - available_kib;
    *used_gib = used_kib / 1048576.0;
    *total_gib = total_kib / 1048576.0;
    *used_percent = 100.0 * used_kib / total_kib;
    return 1;
}

static void make_bar(char *bar, int length, double percent) {
    int filled = (int)(percent * length / 100.0 + 0.5);
    if (filled < 0) filled = 0;
    if (filled > length) filled = length;

    for (int i = 0; i < length; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[length] = '\0';
}

static void draw_status_line(
    int row,
    int width,
    double cpu_percent,
    double memory_used_gib,
    double memory_total_gib,
    double memory_percent
) {
    char status[256];
    char current_time[9] = "--:--:--";
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(current_time, sizeof(current_time), "%H:%M:%S", &local_time);
    }

    if (width >= 96) {
        char cpu_bar[11];
        char memory_bar[11];
        make_bar(cpu_bar, 10, cpu_percent);
        make_bar(memory_bar, 10, memory_percent);
        snprintf(
            status,
            sizeof(status),
            " CPU [%s] %5.1f%% | RAM [%s] %.1f/%.1f GiB (%5.1f%%) | TIME %s ",
            cpu_bar,
            cpu_percent,
            memory_bar,
            memory_used_gib,
            memory_total_gib,
            memory_percent,
            current_time
        );
    } else if (width >= 60) {
        snprintf(
            status,
            sizeof(status),
            " CPU %5.1f%% | RAM %.1f/%.1f GiB (%5.1f%%) | %s ",
            cpu_percent,
            memory_used_gib,
            memory_total_gib,
            memory_percent,
            current_time
        );
    } else if (width >= 36) {
        snprintf(
            status,
            sizeof(status),
            " CPU %.0f%% | RAM %.0f%% | %s ",
            cpu_percent,
            memory_percent,
            current_time
        );
    } else {
        snprintf(
            status,
            sizeof(status),
            " C%.0f%% M%.0f%% %.5s ",
            cpu_percent,
            memory_percent,
            current_time
        );
    }

    int status_length = (int)strlen(status);
    printf("\033[%d;1H\033[48;5;235m\033[38;5;231m", row);

    if (status_length >= width) {
        printf("%.*s", width, status);
    } else {
        int left_fill = (width - status_length) / 2;
        int right_fill = width - status_length - left_fill;

        for (int i = 0; i < left_fill; i++) {
            putchar('-');
        }
        printf("%s", status);
        for (int i = 0; i < right_fill; i++) {
            putchar('-');
        }
    }
    printf("\033[0m");
}

static void packed_rgb(unsigned char color, int *red, int *green, int *blue) {
    *red = ((color >> 4) & 15) * 17;
    *green = ((color >> 1) & 7) * 255 / 7;
    *blue = (color & 1) * 255;
}

static void draw_fireplace_frame(unsigned int frame, int width, int height) {
    int crop_x = 0;
    int crop_y = 0;
    int crop_width = FIREPLACE_FRAME_WIDTH;
    int crop_height = FIREPLACE_FRAME_HEIGHT;

    // A terminal cell is roughly twice as tall as it is wide. Crop without
    // stretching so the fireplace keeps the proportions of the source video.
    if ((long long)width * FIREPLACE_FRAME_HEIGHT
        > (long long)height * 2 * FIREPLACE_FRAME_WIDTH) {
        crop_height = FIREPLACE_FRAME_WIDTH * height * 2 / width;
        if (crop_height < 1) crop_height = 1;
        crop_y = (FIREPLACE_FRAME_HEIGHT - crop_height) / 2;
    } else {
        crop_width = FIREPLACE_FRAME_HEIGHT * width / (height * 2);
        if (crop_width < 1) crop_width = 1;
        crop_x = (FIREPLACE_FRAME_WIDTH - crop_width) / 2;
    }

    const size_t ramp_length = strlen(ASCII_RAMP);
    const size_t frame_offset =
        (size_t)frame * FIREPLACE_FRAME_WIDTH * FIREPLACE_FRAME_HEIGHT;

    for (int y = 0; y < height; y++) {
        int source_y = crop_y + (int)(((long long)y * crop_height) / height);
        uint64_t previous_color = UINT64_MAX;
        printf("\033[%d;1H\033[48;5;16m", y + 1);

        int fade_start = height / 5;
        int fade_end = height * 3 / 4;
        int background_visibility;
        if (y <= fade_start) {
            background_visibility = 0;
        } else if (y >= fade_end) {
            background_visibility = 255;
        } else {
            int fade_range = fade_end - fade_start;
            int progress = (y - fade_start) * 255 / fade_range;
            background_visibility =
                progress * progress * (765 - 2 * progress) / (255 * 255);
        }

        for (int x = 0; x < width; x++) {
            int source_x = crop_x + (int)(((long long)x * crop_width) / width);
            size_t pixel_offset = frame_offset
                + (size_t)source_y * FIREPLACE_FRAME_WIDTH
                + (size_t)source_x;
            unsigned char color = fireplace_frames[pixel_offset];
            int red;
            int green;
            int blue;
            packed_rgb(color, &red, &green, &blue);

            // Keep bright flames visible at every height while the masonry
            // smoothly fades from its full lower-screen color to black above.
            int fire_score = red + 2 * green;
            int fire_visibility = (fire_score - 190) * 255 / 150;
            if (fire_visibility < 0) fire_visibility = 0;
            if (fire_visibility > 255) fire_visibility = 255;
            int foreground_visibility = background_visibility;
            if (fire_visibility > foreground_visibility) {
                foreground_visibility = fire_visibility;
            }

            int display_red = red * foreground_visibility / 255;
            int display_green = green * foreground_visibility / 255;
            int display_blue = blue * foreground_visibility / 255;
            int background_red = red * background_visibility * 2 / (255 * 5);
            int background_green = green * background_visibility * 2 / (255 * 5);
            int background_blue = blue * background_visibility * 2 / (255 * 5);

            int luminance =
                (299 * display_red + 587 * display_green + 114 * display_blue) / 1000;
            luminance += luminance * (255 - luminance) / 510;
            char symbol = ASCII_RAMP[luminance * (ramp_length - 1) / 255];
            uint64_t display_color =
                ((uint64_t)display_red << 40)
                | ((uint64_t)display_green << 32)
                | ((uint64_t)display_blue << 24)
                | ((uint64_t)background_red << 16)
                | ((uint64_t)background_green << 8)
                | (uint64_t)background_blue;
            if (display_color != previous_color) {
                printf(
                    "\033[38;2;%d;%d;%d;48;2;%d;%d;%dm",
                    display_red,
                    display_green,
                    display_blue,
                    background_red,
                    background_green,
                    background_blue
                );
                previous_color = display_color;
            }
            putchar(symbol);
        }
    }
}

int main(void) {
    struct winsize terminal;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) == -1
        || terminal.ws_col == 0
        || terminal.ws_row < 2) {
        fprintf(stderr, "Не удалось определить размер терминала\n");
        return 1;
    }

    signal(SIGINT, stop_animation);
    signal(SIGTERM, stop_animation);

    CpuSample previous_cpu = {0};
    double cpu_percent = 0.0;
    double memory_used_gib = 0.0;
    double memory_total_gib = 0.0;
    double memory_percent = 0.0;
    unsigned int frame = 0;
    unsigned int stats_frame = 0;
    int previous_width = 0;
    int previous_height = 0;

    read_cpu_sample(&previous_cpu);
    read_memory(&memory_used_gib, &memory_total_gib, &memory_percent);
    printf("\033[?25l\033[2J");

    while (keep_running) {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) == -1
            || terminal.ws_col == 0
            || terminal.ws_row < 2) {
            break;
        }

        int width = terminal.ws_col;
        int height = terminal.ws_row;
        int fireplace_height = height - 1;
        if (width != previous_width || height != previous_height) {
            printf("\033[2J");
            previous_width = width;
            previous_height = height;
        }

        draw_fireplace_frame(frame, width, fireplace_height);
        frame = (frame + 1) % FIREPLACE_FRAME_COUNT;

        if (++stats_frame % (FIREPLACE_FRAME_RATE / 2) == 0) {
            CpuSample current_cpu;
            if (read_cpu_sample(&current_cpu) && current_cpu.total > previous_cpu.total) {
                unsigned long long total_delta = current_cpu.total - previous_cpu.total;
                unsigned long long idle_delta = current_cpu.idle - previous_cpu.idle;
                if (idle_delta <= total_delta) {
                    cpu_percent = 100.0 * (total_delta - idle_delta) / total_delta;
                }
                previous_cpu = current_cpu;
            }
            read_memory(&memory_used_gib, &memory_total_gib, &memory_percent);
        }

        draw_status_line(
            height,
            width,
            cpu_percent,
            memory_used_gib,
            memory_total_gib,
            memory_percent
        );
        fflush(stdout);

        struct timespec frame_delay = {
            .tv_sec = 0,
            .tv_nsec = 1000000000L / FIREPLACE_FRAME_RATE,
        };
        nanosleep(&frame_delay, NULL);
    }

    printf("\033[0m\033[?25h");
    fflush(stdout);
    return 0;
}
