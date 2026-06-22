#include "ui.h"
#include "lvgl.h"

#include <stdio.h>

/* Content labels we update on every tick. */
static lv_obj_t *ap3216c_label;
static lv_obj_t *icm20608_label;

/* Create one window filling half the screen width. `title` is shown in the
 * window header; a single multi-line label is returned via *out_label for the
 * caller to update later. */
static lv_obj_t *make_sensor_window(lv_obj_t *parent, const char *title,
                                    lv_obj_t **out_label)
{
    lv_obj_t *win = lv_win_create(parent);
    /* Split the row evenly and use the full height. */
    lv_obj_set_height(win, lv_pct(100));
    lv_obj_set_flex_grow(win, 1);

    lv_obj_t *header = lv_win_add_title(win, title);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);

    lv_obj_t *cont = lv_win_get_content(win);
    lv_obj_set_style_pad_all(cont, 10, 0);

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_label_set_text(label, "...");

    *out_label = label;
    return win;
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* Lay the two windows out in a horizontal row that fills the screen. */
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scr, 6, 0);
    lv_obj_set_style_pad_gap(scr, 6, 0);

    make_sensor_window(scr, "AP3216C  (ALS/PS/IR)", &ap3216c_label);
    make_sensor_window(scr, "ICM20608 (Accel/Gyro)", &icm20608_label);
}

void ui_update_ap3216c(const ap3216c_data_t *d)
{
    if (!ap3216c_label)
        return;

    if (!d->valid) {
        lv_label_set_text(ap3216c_label, "no data\n(driver loaded?)");
        return;
    }

    lv_label_set_text_fmt(ap3216c_label,
                          "ALS (light) : %d\n"
                          "PS  (prox.) : %d\n"
                          "IR          : %d",
                          d->als, d->ps, d->ir);
}

void ui_update_icm20608(const icm20608_data_t *d)
{
    if (!icm20608_label)
        return;

    if (!d->valid) {
        lv_label_set_text(icm20608_label, "no data\n(driver loaded?)");
        return;
    }

    lv_label_set_text_fmt(icm20608_label,
                          "Accel (m/s^2)\n"
                          "  x:%7.2f  y:%7.2f  z:%7.2f\n"
                          "Gyro (rad/s)\n"
                          "  x:%7.2f  y:%7.2f  z:%7.2f\n"
                          "Temp: %.1f C",
                          d->accel[0], d->accel[1], d->accel[2],
                          d->gyro[0],  d->gyro[1],  d->gyro[2],
                          d->temp_c);
}
