/**
 * @file lv_conf.h
 * LVGL v9.3 configuration for the i.MX6ULL ATK 4.3" RGB LCD (eLCDIF / DRM mxsfb).
 *
 * Only the options that differ from LVGL's built-in defaults are set here;
 * everything else falls back to the defaults in lv_conf_internal.h.
 *
 * NOTE on LV_COLOR_DEPTH: it MUST match the framebuffer's bits-per-pixel.
 * The kernel uses DRM_MXSFB with fbdev emulation, which typically exposes
 * /dev/fb0 as 32 bpp (XRGB8888) -> keep 32 below.  If the panel comes up as
 * RGB565 (16 bpp), the app prints the real bpp at startup; change this to 16
 * and rebuild.  The app aborts with a clear message on a mismatch.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR / DISPLAY
 *====================*/
#define LV_COLOR_DEPTH 32

/*====================
   MEMORY
 *====================*/
/* Use the C library malloc/free so we are not bound by a fixed LV_MEM_SIZE. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   OS / TICK
 *====================*/
#define LV_USE_OS   LV_OS_NONE
/* We feed LVGL its tick via lv_tick_set_cb() in main.c (clock_gettime based). */

/* Default screen refresh / input read period (ms). */
#define LV_DEF_REFR_PERIOD  30

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF   1
#endif

/*====================
   LINUX BACKENDS
 *====================*/
/* Framebuffer display backend (/dev/fb0). No touch -> evdev stays disabled. */
#define LV_USE_LINUX_FBDEV          1
#define LV_LINUX_FBDEV_BSD          0
#define LV_LINUX_FBDEV_RENDER_MODE  LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
#define LV_LINUX_FBDEV_BUFFER_SIZE  60

/*====================
   FONTS
 *====================*/
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGETS
 *====================*/
/* lv_win is used for the per-sensor windows; it is enabled by default but
   make the dependency explicit. */
#define LV_USE_WIN  1
#define LV_USE_LABEL 1

#endif /* LV_CONF_H */
