/**
 * @file main.c
 * LVGL v9 application for the ATK 4.3" RGB LCD on i.MX6ULL.
 *
 * Renders two sensor windows on /dev/fb0:
 *   - AP3216C  (i2cd, IIO): ambient light / proximity / IR
 *   - ICM20608 (spid, IIO): 3-axis accel + 3-axis gyro + temperature
 *
 * Sensor samples are read from the IIO sysfs interface and pushed to the UI
 * by a periodic LVGL timer. Display-only: no input device is registered.
 */
#include "lvgl.h"
#include "ui.h"
#include "sensors.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#define FB_DEVICE   "/dev/fb0"
#define UPDATE_MS   200

/* Monotonic millisecond tick source for LVGL (lv_tick_set_cb). */
static uint32_t tick_get_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* Verify the framebuffer's bits-per-pixel matches LV_COLOR_DEPTH; a mismatch
 * produces garbled output, so fail loudly with a hint instead. */
static int check_fb_depth(void)
{
    int fd = open(FB_DEVICE, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "cannot open %s (is the LCD/DRM fbdev present?)\n", FB_DEVICE);
        return -1;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        fprintf(stderr, "FBIOGET_VSCREENINFO failed on %s\n", FB_DEVICE);
        close(fd);
        return -1;
    }
    close(fd);

    printf("fb: %ux%u, %u bpp (LV_COLOR_DEPTH=%d)\n",
           vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, LV_COLOR_DEPTH);

    if ((int)vinfo.bits_per_pixel != LV_COLOR_DEPTH) {
        fprintf(stderr,
                "ERROR: framebuffer is %u bpp but LVGL was built for %d bpp.\n"
                "       Set LV_COLOR_DEPTH to %u in lv_conf.h and rebuild.\n",
                vinfo.bits_per_pixel, LV_COLOR_DEPTH, vinfo.bits_per_pixel);
        return -1;
    }
    return 0;
}

/* Periodic callback: read both sensors and refresh the windows. */
static void update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    ap3216c_data_t als;
    icm20608_data_t imu;

    sensors_read_ap3216c(&als);
    sensors_read_icm20608(&imu);

    ui_update_ap3216c(&als);
    ui_update_icm20608(&imu);
}

int main(void)
{
    if (check_fb_depth() != 0)
        return EXIT_FAILURE;

    sensors_init();

    lv_init();
    lv_tick_set_cb(tick_get_ms);

    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp) {
        fprintf(stderr, "lv_linux_fbdev_create() failed\n");
        return EXIT_FAILURE;
    }
    lv_linux_fbdev_set_file(disp, FB_DEVICE);

    ui_create();
    lv_timer_create(update_timer_cb, UPDATE_MS, NULL);

    /* Main loop: let LVGL render and tell us how long we may sleep. */
    while (1) {
        uint32_t idle = lv_timer_handler();
        if (idle == LV_NO_TIMER_READY || idle > 100)
            idle = 100;
        usleep(idle * 1000);
    }

    return EXIT_SUCCESS;
}
