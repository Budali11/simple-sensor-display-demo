/**
 * @file ui.h
 * LVGL UI: one window (lv_win) per sensor, laid out side by side.
 */
#ifndef UI_H
#define UI_H

#include "sensors.h"

/* Build the two sensor windows on the active screen. Call once after lv_init
 * and after the display has been created. */
void ui_create(void);

/* Refresh the contents of each window with the latest sample. */
void ui_update_ap3216c(const ap3216c_data_t *d);
void ui_update_icm20608(const icm20608_data_t *d);

#endif /* UI_H */
