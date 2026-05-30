#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_wpm_wave {
    sys_snode_t node;
    lv_obj_t *obj; // lv_chart
    lv_chart_series_t *series;
};

int zmk_widget_wpm_wave_init(struct zmk_widget_wpm_wave *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_wpm_wave_obj(struct zmk_widget_wpm_wave *widget);
