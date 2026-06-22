#include "sensors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define IIO_DEVICES_DIR "/sys/bus/iio/devices"

/* Resolved device directories, e.g. "/sys/bus/iio/devices/iio:device0". */
static char ap3216c_dir[256];
static char icm20608_dir[256];

/* Read the whole content of a small sysfs file into buf. Returns true on ok. */
static bool read_file(const char *path, char *buf, size_t buflen)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;

    size_t n = fread(buf, 1, buflen - 1, fp);
    fclose(fp);
    if (n == 0)
        return false;

    buf[n] = '\0';
    /* Strip trailing newline. */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    return true;
}

/* Read an integer attribute "<dir>/<attr>". */
static bool read_int(const char *dir, const char *attr, int *out)
{
    char path[320];
    char buf[64];
    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    if (!read_file(path, buf, sizeof(buf)))
        return false;
    *out = (int)strtol(buf, NULL, 10);
    return true;
}

/* Read a floating-point attribute "<dir>/<attr>". */
static bool read_double(const char *dir, const char *attr, double *out)
{
    char path[320];
    char buf[64];
    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    if (!read_file(path, buf, sizeof(buf)))
        return false;
    *out = strtod(buf, NULL);
    return true;
}

/* Scan IIO_DEVICES_DIR for an iio:deviceN whose "name" matches `name`.
 * On success copies the full directory path into out. */
static bool find_iio_device(const char *name, char *out, size_t outlen)
{
    DIR *d = opendir(IIO_DEVICES_DIR);
    if (!d) {
        fprintf(stderr, "sensors: cannot open %s\n", IIO_DEVICES_DIR);
        return false;
    }

    struct dirent *ent;
    bool found = false;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "iio:device", 10) != 0)
            continue;

        char dir[256];
        char namebuf[64];
        char namepath[320];
        snprintf(dir, sizeof(dir), "%s/%s", IIO_DEVICES_DIR, ent->d_name);

        snprintf(namepath, sizeof(namepath), "%s/name", dir);
        if (read_file(namepath, namebuf, sizeof(namebuf)) &&
            strcmp(namebuf, name) == 0) {
            snprintf(out, outlen, "%s", dir);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

int sensors_init(void)
{
    int count = 0;
    ap3216c_dir[0] = '\0';
    icm20608_dir[0] = '\0';

    if (find_iio_device("ap3216c", ap3216c_dir, sizeof(ap3216c_dir))) {
        printf("sensors: AP3216C at %s\n", ap3216c_dir);
        count++;
    } else {
        fprintf(stderr, "sensors: AP3216C (ap3216c) not found\n");
    }

    if (find_iio_device("icm20608", icm20608_dir, sizeof(icm20608_dir))) {
        printf("sensors: ICM20608 at %s\n", icm20608_dir);
        count++;
    } else {
        fprintf(stderr, "sensors: ICM20608 (icm20608) not found\n");
    }

    return count;
}

bool sensors_read_ap3216c(ap3216c_data_t *out)
{
    memset(out, 0, sizeof(*out));
    if (ap3216c_dir[0] == '\0')
        return false;

    bool ok = true;
    ok &= read_int(ap3216c_dir, "in_illuminance_clear_raw", &out->als);
    ok &= read_int(ap3216c_dir, "in_proximity_raw",         &out->ps);
    ok &= read_int(ap3216c_dir, "in_illuminance_ir_raw",    &out->ir);

    out->valid = ok;
    return ok;
}

bool sensors_read_icm20608(icm20608_data_t *out)
{
    memset(out, 0, sizeof(*out));
    if (icm20608_dir[0] == '\0')
        return false;

    bool ok = true;
    ok &= read_int(icm20608_dir, "in_accel_x_raw",   &out->accel_raw[0]);
    ok &= read_int(icm20608_dir, "in_accel_y_raw",   &out->accel_raw[1]);
    ok &= read_int(icm20608_dir, "in_accel_z_raw",   &out->accel_raw[2]);
    ok &= read_int(icm20608_dir, "in_anglvel_x_raw", &out->gyro_raw[0]);
    ok &= read_int(icm20608_dir, "in_anglvel_y_raw", &out->gyro_raw[1]);
    ok &= read_int(icm20608_dir, "in_anglvel_z_raw", &out->gyro_raw[2]);
    ok &= read_int(icm20608_dir, "in_temp_raw",      &out->temp_raw);

    /* Scales are shared-by-type; absence is non-fatal (fall back to 0 -> raw only). */
    if (!read_double(icm20608_dir, "in_accel_scale",   &out->accel_scale))
        out->accel_scale = 0.0;
    if (!read_double(icm20608_dir, "in_anglvel_scale", &out->gyro_scale))
        out->gyro_scale = 0.0;

    for (int i = 0; i < 3; i++) {
        out->accel[i] = out->accel_raw[i] * out->accel_scale;
        out->gyro[i]  = out->gyro_raw[i]  * out->gyro_scale;
    }
    /* ICM20608 temperature: T(degC) = raw/326.8 + 25.0 (datasheet). */
    out->temp_c = out->temp_raw / 326.8 + 25.0;

    out->valid = ok;
    return ok;
}
