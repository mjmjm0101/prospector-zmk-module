#include "wpm_wave.h"

#include <zephyr/kernel.h>

#include <zmk/display.h>
#include <zmk/wpm.h>

#include "display_colors.h"

// A scrolling WPM history graph drawn behind the layer name. Each tick the
// newest WPM sample is pushed in from the right and the bars shift left, so it
// reads like a waveform reacting to typing. Kept dim/translucent so the layer
// name stays readable on top.

#define WAVE_W 260
#define WAVE_H 90
#define WPM_MAX 120
#define BAR_W 8
#define BAR_GAP 2
#define TICK_MS 150
#define WAVE_OPA LV_OPA_40

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct k_work_delayable wave_work;
static uint8_t history[WPM_WAVE_BARS]; // bar heights, 0..WAVE_H

static int bar_x(int i) {
    int total = WPM_WAVE_BARS * BAR_W + (WPM_WAVE_BARS - 1) * BAR_GAP;
    int start_x = (WAVE_W - total) / 2;
    return start_x + i * (BAR_W + BAR_GAP);
}

static void wave_render(void) {
    struct zmk_widget_wpm_wave *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        for (int i = 0; i < WPM_WAVE_BARS; i++) {
            int h = history[i];
            if (h < 1) {
                h = 1;
            }
            lv_obj_set_size(widget->bars[i], BAR_W, h);
            lv_obj_set_pos(widget->bars[i], bar_x(i), WAVE_H - h);
        }
    }
}

static void wave_tick(struct k_work *work) {
    for (int i = 0; i < WPM_WAVE_BARS - 1; i++) {
        history[i] = history[i + 1];
    }

    int wpm = zmk_wpm_get_state();
    int h = wpm * WAVE_H / WPM_MAX;
    if (h > WAVE_H) {
        h = WAVE_H;
    }
    history[WPM_WAVE_BARS - 1] = (uint8_t)h;

    wave_render();
    k_work_schedule(&wave_work, K_MSEC(TICK_MS));
}

int zmk_widget_wpm_wave_init(struct zmk_widget_wpm_wave *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, WAVE_W, WAVE_H);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < WPM_WAVE_BARS; i++) {
        widget->bars[i] = lv_obj_create(widget->obj);
        lv_obj_set_size(widget->bars[i], BAR_W, 1);
        lv_obj_set_pos(widget->bars[i], bar_x(i), WAVE_H - 1);
        lv_obj_set_style_bg_color(widget->bars[i], lv_color_hex(DISPLAY_COLOR_WPM_BAR_ACTIVE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(widget->bars[i], WAVE_OPA, LV_PART_MAIN);
        lv_obj_set_style_border_width(widget->bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(widget->bars[i], 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(widget->bars[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(widget->bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    sys_slist_append(&widgets, &widget->node);

    k_work_init_delayable(&wave_work, wave_tick);
    k_work_schedule(&wave_work, K_MSEC(TICK_MS));

    return 0;
}

lv_obj_t *zmk_widget_wpm_wave_obj(struct zmk_widget_wpm_wave *widget) {
    return widget->obj;
}
