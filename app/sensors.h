/**
 * @file sensors.h
 * Read AP3216C (i2cd) and ICM20608 (spid) sample data from the Linux IIO
 * sysfs interface (/sys/bus/iio/devices/iio:deviceN/...).
 *
 * The IIO device index (device0/device1) depends on probe order, so devices
 * are located at runtime by matching the "name" attribute rather than a
 * hard-coded index.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>

/* AP3216C: ambient light (ALS), proximity (PS), infrared (IR) — raw counts. */
typedef struct {
    bool valid;
    int  als;   /* in_illuminance_clear_raw */
    int  ps;    /* in_proximity_raw         */
    int  ir;    /* in_illuminance_ir_raw    */
} ap3216c_data_t;

/* ICM20608: 3-axis accel + 3-axis gyro + temperature. */
typedef struct {
    bool   valid;
    int    accel_raw[3];     /* x,y,z  in_accel_{x,y,z}_raw   */
    int    gyro_raw[3];      /* x,y,z  in_anglvel_{x,y,z}_raw */
    int    temp_raw;         /* in_temp_raw                   */
    double accel_scale;      /* in_accel_scale   (m/s^2 / LSB)*/
    double gyro_scale;       /* in_anglvel_scale (rad/s / LSB)*/
    double accel[3];         /* scaled, m/s^2                 */
    double gyro[3];          /* scaled, rad/s                 */
    double temp_c;           /* derived, degrees C            */
} icm20608_data_t;

/* Locate both IIO devices by name. Returns the number of sensors found (0..2).
 * Missing sensors simply read as invalid later; this is not fatal. */
int  sensors_init(void);

bool sensors_read_ap3216c(ap3216c_data_t *out);
bool sensors_read_icm20608(icm20608_data_t *out);

#endif /* SENSORS_H */
