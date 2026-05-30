#include "wpm_wave.h"

#include <zephyr/kernel.h>

#include <zmk/display.h>
#include <zmk/wpm.h>

#include "display_colors.h"

// A scrolling WPM line graph drawn behind the layer name. Each tick the current
// WPM (eased toward its target) is pushed in from the right (lv_chart SHIFT
// mode), giving an oscilloscope-like line behind the text. lv_chart draws plain
// polylines, so smoothness comes from many closely-spaced points plus easing
// the value (no hard vertical steps) and rounded line joins.

#define WAVE_W 260
#define WAVE_H 90
#define WPM_MAX 120
#define WAVE_POINTS 130   // ~2px apart across 260px
#define TICK_FAST_MS 50   // while there is motion
#define TICK_SLOW_MS 400  // while idle/flat, to cut redraw load
#define EASE_FACTOR 0.20f // value approaches target by this fraction per tick
#define WAVE_OPA LV_OPA_50
#define WAVE_LINE_WIDTH 2

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct k_work_delayable wave_work;
static float displayed; // eased WPM

static void wave_tick(struct k_work *work) {
    float target = (float)zmk_wpm_get_state();
    if (target > WPM_MAX) {
        target = WPM_MAX;
    }

    float diff = target - displayed;
    bool moving = (diff > 0.5f || diff < -0.5f);
    displayed += diff * EASE_FACTOR;
    if (!moving) {
        displayed = target;
    }

    int value = (int)(displayed + 0.5f);

    struct zmk_widget_wpm_wave *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_chart_set_next_value(widget->obj, widget->series, value);
    }

    bool active = moving || target > 0.5f || displayed > 0.5f;
    k_work_schedule(&wave_work, K_MSEC(active ? TICK_FAST_MS : TICK_SLOW_MS));
}

int zmk_widget_wpm_wave_init(struct zmk_widget_wpm_wave *widget, lv_obj_t *parent) {
    widget->obj = lv_chart_create(parent);
    lv_obj_set_size(widget->obj, WAVE_W, WAVE_H);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_set_type(widget->obj, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(widget->obj, WAVE_POINTS);
    lv_chart_set_range(widget->obj, LV_CHART_AXIS_PRIMARY_Y, 0, WPM_MAX);
    lv_chart_set_div_line_count(widget->obj, 0, 0);
    lv_chart_set_update_mode(widget->obj, LV_CHART_UPDATE_MODE_SHIFT);

    // Continuous, rounded line; no dot markers.
    lv_obj_set_style_line_width(widget->obj, WAVE_LINE_WIDTH, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(widget->obj, WAVE_OPA, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(widget->obj, true, LV_PART_ITEMS);
    lv_obj_set_style_width(widget->obj, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(widget->obj, 0, LV_PART_INDICATOR);

    widget->series = lv_chart_add_series(widget->obj,
                                         lv_color_hex(DISPLAY_COLOR_WPM_BAR_ACTIVE),
                                         LV_CHART_AXIS_PRIMARY_Y);

    sys_slist_append(&widgets, &widget->node);

    k_work_init_delayable(&wave_work, wave_tick);
    k_work_schedule(&wave_work, K_MSEC(TICK_FAST_MS));

    return 0;
}

lv_obj_t *zmk_widget_wpm_wave_obj(struct zmk_widget_wpm_wave *widget) {
    return widget->obj;
}
