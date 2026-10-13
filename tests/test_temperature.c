#define _POSIX_C_SOURCE 200809L

#include "temperature.h"

#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_directory(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path, const char *value) {
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(value, file) >= 0);
    assert(fclose(file) == 0);
}

static void path_join(char *destination, size_t size, const char *left, const char *right) {
    int length = snprintf(destination, size, "%s/%s", left, right);
    assert(length > 0 && (size_t)length < size);
}

static void create_sensor(
    const char *root,
    const char *hwmon,
    const char *chip,
    int index,
    const char *label,
    const char *temperature,
    const char *critical
) {
    char directory[FIREC_PATH_MAX];
    char path[FIREC_PATH_MAX];
    char filename[64];
    path_join(directory, sizeof(directory), root, hwmon);
    make_directory(directory);

    path_join(path, sizeof(path), directory, "name");
    write_file(path, chip);

    snprintf(filename, sizeof(filename), "temp%d_input", index);
    path_join(path, sizeof(path), directory, filename);
    write_file(path, temperature);

    if (label != NULL) {
        snprintf(filename, sizeof(filename), "temp%d_label", index);
        path_join(path, sizeof(path), directory, filename);
        write_file(path, label);
    }
    if (critical != NULL) {
        snprintf(filename, sizeof(filename), "temp%d_crit", index);
        path_join(path, sizeof(path), directory, filename);
        write_file(path, critical);
    }
}

static void add_sensor(
    const char *root,
    const char *hwmon,
    int index,
    const char *label,
    const char *temperature,
    const char *critical
) {
    char directory[FIREC_PATH_MAX];
    char path[FIREC_PATH_MAX];
    char filename[64];
    path_join(directory, sizeof(directory), root, hwmon);

    snprintf(filename, sizeof(filename), "temp%d_input", index);
    path_join(path, sizeof(path), directory, filename);
    write_file(path, temperature);

    snprintf(filename, sizeof(filename), "temp%d_label", index);
    path_join(path, sizeof(path), directory, filename);
    write_file(path, label);

    if (critical != NULL) {
        snprintf(filename, sizeof(filename), "temp%d_crit", index);
        path_join(path, sizeof(path), directory, filename);
        write_file(path, critical);
    }
}

static void remove_file(const char *root, const char *hwmon, const char *filename) {
    char directory[FIREC_PATH_MAX];
    char path[FIREC_PATH_MAX];
    path_join(directory, sizeof(directory), root, hwmon);
    path_join(path, sizeof(path), directory, filename);
    assert(unlink(path) == 0);
}

static void remove_tree(const char *path) {
    DIR *directory = opendir(path);
    assert(directory != NULL);

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[FIREC_PATH_MAX];
        path_join(child, sizeof(child), path, entry->d_name);
        struct stat status;
        assert(lstat(child, &status) == 0);
        if (S_ISDIR(status.st_mode)) {
            remove_tree(child);
        } else {
            assert(unlink(child) == 0);
        }
    }
    assert(closedir(directory) == 0);
    assert(rmdir(path) == 0);
}

static void test_package_temperature_is_preferred(void) {
    char root[] = "/tmp/firec-temperature-test-XXXXXX";
    assert(mkdtemp(root) != NULL);

    create_sensor(root, "hwmon0", "nvme\n", 1, "Composite\n", "49000\n", NULL);
    create_sensor(
        root,
        "hwmon1",
        "coretemp\n",
        1,
        "Package id 0\n",
        "72000\n",
        "100000\n"
    );
    add_sensor(root, "hwmon1", 2, "Core 0\n", "81000\n", "100000\n");
    add_sensor(root, "hwmon1", 3, "Core 1\n", "79000\n", "100000\n");
    create_sensor(root, "hwmon2", "thinkpad\n", 1, "CPU\n", "83000\n", NULL);

    TemperatureSource source;
    assert(temperature_source_discover(&source, root));
    assert(source.count == 1);
    assert(strstr(source.paths[0], "hwmon1/temp1_input") != NULL);
    assert(fabs(source.critical_celsius - 100.0) < 0.001);

    double temperature = 0.0;
    assert(temperature_source_read(&source, &temperature));
    assert(fabs(temperature - 72.0) < 0.001);

    /* Without a package sensor, aggregate the hottest CPU core. */
    remove_file(root, "hwmon1", "temp1_input");
    remove_file(root, "hwmon2", "temp1_input");
    assert(temperature_source_discover(&source, root));
    assert(source.count == 2);
    assert(temperature_source_read(&source, &temperature));
    assert(fabs(temperature - 81.0) < 0.001);

    remove_tree(root);
}

static void test_hottest_package_is_used_on_multi_socket_systems(void) {
    char root[] = "/tmp/firec-temperature-test-XXXXXX";
    assert(mkdtemp(root) != NULL);

    create_sensor(
        root,
        "hwmon0",
        "coretemp\n",
        1,
        "Package id 0\n",
        "72000\n",
        "100000\n"
    );
    create_sensor(
        root,
        "hwmon1",
        "coretemp\n",
        1,
        "Package id 1\n",
        "84000\n",
        "95000\n"
    );

    TemperatureSource source;
    assert(temperature_source_discover(&source, root));
    assert(source.count == 2);
    assert(fabs(source.critical_celsius - 95.0) < 0.001);

    double temperature = 0.0;
    assert(temperature_source_read(&source, &temperature));
    assert(fabs(temperature - 84.0) < 0.001);

    char directory[FIREC_PATH_MAX];
    char fault_path[FIREC_PATH_MAX];
    path_join(directory, sizeof(directory), root, "hwmon1");
    path_join(fault_path, sizeof(fault_path), directory, "temp1_fault");
    write_file(fault_path, "1\n");
    assert(temperature_source_read(&source, &temperature));
    assert(fabs(temperature - 72.0) < 0.001);

    remove_tree(root);
}

static void test_faulty_and_non_cpu_sensors_are_ignored(void) {
    char root[] = "/tmp/firec-temperature-test-XXXXXX";
    assert(mkdtemp(root) != NULL);

    create_sensor(root, "hwmon0", "nvme\n", 1, "Composite\n", "49000\n", NULL);
    create_sensor(root, "hwmon1", "coretemp\n", 1, "Package id 0\n", "72000\n", NULL);

    char fault_path[FIREC_PATH_MAX];
    char directory[FIREC_PATH_MAX];
    path_join(directory, sizeof(directory), root, "hwmon1");
    path_join(fault_path, sizeof(fault_path), directory, "temp1_fault");
    write_file(fault_path, "1\n");

    TemperatureSource source;
    assert(!temperature_source_discover(&source, root));
    assert(source.count == 0);
    assert(fabs(source.critical_celsius - 100.0) < 0.001);

    remove_tree(root);
}

int main(void) {
    test_package_temperature_is_preferred();
    test_hottest_package_is_used_on_multi_socket_systems();
    test_faulty_and_non_cpu_sensors_are_ignored();
    puts("temperature tests: OK");
    return 0;
}
