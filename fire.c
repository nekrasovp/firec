#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "fireplace_frames.h"
#include "temperature.h"
#include "ui.h"

static const char ASCII_RAMP[] = " .,:;irsXA253hMHGS#9B&@";
static volatile sig_atomic_t keep_running = 1;

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} CpuSample;

typedef struct {
    int shellsaver;
    unsigned long seconds_limit;
} Options;

typedef struct {
    int alternate_screen;
    int termios_saved;
    struct termios saved_termios;
} TerminalSession;

static void stop_animation(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void print_usage(FILE *stream) {
    fprintf(
        stream,
        "Usage: fire [--shellsaver [--seconds N]]\n"
        "\n"
        "Without arguments, show the fireplace with Linux system metrics.\n"
        "--shellsaver  Show only the original full-screen fireplace and exit\n"
        "              on any key. No CPU, RAM, clock, or sensor data is read.\n"
        "--seconds N   Stop shellsaver mode after N seconds.\n"
        "--help        Show this help.\n"
    );
}

static int parse_seconds(const char *text, unsigned long *seconds) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1) {
        return 0;
    }
    *seconds = value;
    return 1;
}

static int parse_options(int argc, char **argv, Options *options) {
    options->shellsaver = 0;
    options->seconds_limit = 0;

    if (argc == 1) {
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(stdout);
        return 1;
    }
    if (strcmp(argv[1], "--shellsaver") != 0) {
        fprintf(stderr, "fire: unknown argument: %s (try --help)\n", argv[1]);
        return -1;
    }

    options->shellsaver = 1;
    for (int index = 2; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            print_usage(stdout);
            return 1;
        }
        if (strcmp(argv[index], "--seconds") == 0) {
            if (++index >= argc || !parse_seconds(argv[index], &options->seconds_limit)) {
                fprintf(stderr, "fire: --seconds requires a positive integer\n");
                return -1;
            }
            continue;
        }
        fprintf(stderr, "fire: unknown shellsaver argument: %s (try --help)\n", argv[index]);
        return -1;
    }
    return 0;
}

static int enter_terminal(TerminalSession *session, int shellsaver) {
    memset(session, 0, sizeof(*session));
    if (!shellsaver) {
        printf("\033[?25l\033[2J");
        return 1;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "fire: shellsaver mode requires an interactive terminal\n");
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &session->saved_termios) == -1) {
        perror("fire: tcgetattr");
        return 0;
    }
    session->termios_saved = 1;

    struct termios raw = session->saved_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_iflag &= (tcflag_t)~(ICRNL | IXON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("fire: tcsetattr");
        return 0;
    }

    session->alternate_screen = 1;
    printf("\033[?1049h\033[?25l\033[2J");
    return 1;
}

static void leave_terminal(TerminalSession *session) {
    printf("\033[0m\033[?25h");
    if (session->alternate_screen) {
        printf("\033[?1049l");
    }
    fflush(stdout);
    if (session->termios_saved) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &session->saved_termios);
    }
}

static int wait_for_frame_or_input(int watch_input) {
    const long frame_nanoseconds = 1000000000L / FIREPLACE_FRAME_RATE;
    if (!watch_input) {
        struct timespec frame_delay = {
            .tv_sec = 0,
            .tv_nsec = frame_nanoseconds,
        };
        while (nanosleep(&frame_delay, &frame_delay) == -1 && errno == EINTR) {
            if (!keep_running) return 0;
        }
        return 0;
    }

    fd_set input;
    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = (suseconds_t)(frame_nanoseconds / 1000),
    };
    int ready = select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout);

    if (ready > 0 && FD_ISSET(STDIN_FILENO, &input)) {
        char input_buffer[128];
        ssize_t bytes_read = read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
        return bytes_read > 0;
    }
    return 0;
}

static double elapsed_seconds(const struct timespec *started) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return 0.0;
    }
    return (double)(now.tv_sec - started->tv_sec)
        + (double)(now.tv_nsec - started->tv_nsec) / 1000000000.0;
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

static int read_memory(double *used_percent) {
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
    *used_percent = 100.0 * used_kib / total_kib;
    return 1;
}

static void draw_status_line(
    int row,
    int width,
    double cpu_percent,
    double memory_percent,
    int temperature_available,
    double temperature_celsius
) {
    char status[256];
    char current_time[9] = "--:--:--";
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) != NULL) {
        strftime(current_time, sizeof(current_time), "%H:%M:%S", &local_time);
    }

    format_status_line(
        status,
        sizeof(status),
        width,
        cpu_percent,
        memory_percent,
        temperature_available,
        temperature_celsius,
        current_time
    );

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

static int apply_gain(int value, int gain) {
    value = value * gain / 256;
    return value > 255 ? 255 : value;
}

static void packed_rgb(unsigned char color, int *red, int *green, int *blue) {
    *red = ((color >> 4) & 15) * 17;
    *green = ((color >> 1) & 7) * 255 / 7;
    *blue = (color & 1) * 255;
}

static void draw_fireplace_frame(
    unsigned int frame,
    int width,
    int height,
    const FireStyle *style
) {
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
            int scene_red = apply_gain(red, style->scene_red_gain);
            int scene_green = apply_gain(green, style->scene_green_gain);
            int scene_blue = apply_gain(blue, style->scene_blue_gain);
            int flame_red = apply_gain(red, style->flame_red_gain);
            int flame_green = apply_gain(green, style->flame_green_gain);
            int flame_blue = apply_gain(blue, style->flame_blue_gain);

            // Keep bright flames visible at every height while the masonry
            // smoothly fades from its full lower-screen color to black above.
            int fire_score = flame_red + 2 * flame_green;
            int fire_visibility = (fire_score - 190) * 255 / 150;
            if (fire_visibility < 0) fire_visibility = 0;
            if (fire_visibility > 255) fire_visibility = 255;
            int foreground_visibility = background_visibility;
            if (fire_visibility > foreground_visibility) {
                foreground_visibility = fire_visibility;
            }

            int mixed_red = (
                scene_red * (255 - fire_visibility) + flame_red * fire_visibility
            ) / 255;
            int mixed_green = (
                scene_green * (255 - fire_visibility) + flame_green * fire_visibility
            ) / 255;
            int mixed_blue = (
                scene_blue * (255 - fire_visibility) + flame_blue * fire_visibility
            ) / 255;
            int display_red = mixed_red * foreground_visibility / 255;
            int display_green = mixed_green * foreground_visibility / 255;
            int display_blue = mixed_blue * foreground_visibility / 255;
            int background_red = scene_red * background_visibility * 2 / (255 * 5);
            int background_green = scene_green * background_visibility * 2 / (255 * 5);
            int background_blue = scene_blue * background_visibility * 2 / (255 * 5);

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

int main(int argc, char **argv) {
    Options options;
    int parse_result = parse_options(argc, argv, &options);
    if (parse_result > 0) return 0;
    if (parse_result < 0) return 2;

    struct winsize terminal;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) == -1
        || terminal.ws_col == 0
        || terminal.ws_row < (options.shellsaver ? 1 : 2)) {
        fprintf(stderr, "Не удалось определить размер терминала\n");
        return 1;
    }

    signal(SIGINT, stop_animation);
    signal(SIGTERM, stop_animation);
    signal(SIGHUP, stop_animation);
    signal(SIGQUIT, stop_animation);
    signal(SIGTSTP, stop_animation);

    CpuSample previous_cpu = {0};
    double cpu_percent = 0.0;
    double memory_percent = 0.0;
    TemperatureSource temperature_source = {0};
    double temperature_celsius = 0.0;
    int temperature_available = 0;
    unsigned int temperature_failures = 0;
    unsigned int frame = 0;
    unsigned int stats_frame = 0;
    int previous_width = 0;
    int previous_height = 0;

    if (!options.shellsaver) {
        read_cpu_sample(&previous_cpu);
        read_memory(&memory_percent);
        if (temperature_source_discover(&temperature_source, "/sys/class/hwmon")) {
            temperature_available = temperature_source_read(
                &temperature_source,
                &temperature_celsius
            );
        }
    }

    TerminalSession terminal_session;
    if (!enter_terminal(&terminal_session, options.shellsaver)) {
        leave_terminal(&terminal_session);
        return 1;
    }
    struct timespec started = {0};
    if (options.shellsaver && options.seconds_limit > 0) {
        (void)clock_gettime(CLOCK_MONOTONIC, &started);
    }

    while (keep_running) {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) == -1
            || terminal.ws_col == 0
            || terminal.ws_row < (options.shellsaver ? 1 : 2)) {
            break;
        }

        int width = terminal.ws_col;
        int height = terminal.ws_row;
        int fireplace_height = options.shellsaver ? height : height - 1;
        if (width != previous_width || height != previous_height) {
            printf("\033[2J");
            previous_width = width;
            previous_height = height;
        }

        FireStyle fire_style = fire_style_from_temperature(
            options.shellsaver ? 0 : temperature_available,
            temperature_celsius,
            temperature_source.critical_celsius
        );
        draw_fireplace_frame(frame, width, fireplace_height, &fire_style);
        frame = (frame + 1) % FIREPLACE_FRAME_COUNT;

        if (!options.shellsaver
            && ++stats_frame % (FIREPLACE_FRAME_RATE / 2) == 0) {
            CpuSample current_cpu;
            if (read_cpu_sample(&current_cpu) && current_cpu.total > previous_cpu.total) {
                unsigned long long total_delta = current_cpu.total - previous_cpu.total;
                unsigned long long idle_delta = current_cpu.idle - previous_cpu.idle;
                if (idle_delta <= total_delta) {
                    cpu_percent = 100.0 * (total_delta - idle_delta) / total_delta;
                }
                previous_cpu = current_cpu;
            }
            read_memory(&memory_percent);

            double current_temperature = 0.0;
            if (temperature_source_read(&temperature_source, &current_temperature)) {
                if (temperature_available) {
                    temperature_celsius = temperature_celsius * 0.75
                        + current_temperature * 0.25;
                } else {
                    temperature_celsius = current_temperature;
                }
                temperature_available = 1;
                temperature_failures = 0;
            } else {
                temperature_failures++;
                if (temperature_failures >= 3) {
                    temperature_available = 0;
                }
                if (temperature_failures >= 60) {
                    (void)temperature_source_discover(
                        &temperature_source,
                        "/sys/class/hwmon"
                    );
                    temperature_failures = 0;
                }
            }
        }

        if (!options.shellsaver) {
            draw_status_line(
                height,
                width,
                cpu_percent,
                memory_percent,
                temperature_available,
                temperature_celsius
            );
        }
        fflush(stdout);

        if (options.shellsaver && options.seconds_limit > 0
            && elapsed_seconds(&started) >= options.seconds_limit) {
            break;
        }
        if (wait_for_frame_or_input(options.shellsaver)) {
            break;
        }
    }

    leave_terminal(&terminal_session);
    return 0;
}
