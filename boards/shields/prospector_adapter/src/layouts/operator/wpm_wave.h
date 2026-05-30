#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define WPM_WAVE_BARS 26

struct zmk_widget_wpm_wave {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *bars[WPM_WAVE_BARS];
};

int zmk_widget_wpm_wave_init(struct zmk_widget_wpm_wave *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_wpm_wave_obj(struct zmk_widget_wpm_wave *widget);
