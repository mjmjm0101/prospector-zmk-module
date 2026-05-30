#include <lvgl.h>

#include "modifier_indicator.h"
#include "layer_name.h"
#include "layer_display.h"
#include "battery_circles.h"
#include "output.h"

#include <fonts.h>

static struct zmk_widget_modifier_indicator modifier_indicator_widget;
static struct zmk_widget_layer_name layer_name_widget;
static struct zmk_widget_layer_display layer_display_widget;
static struct zmk_widget_battery_circles battery_circles_widget;
static struct zmk_widget_output output_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    zmk_widget_modifier_indicator_init(&modifier_indicator_widget, screen);
    lv_obj_set_pos(zmk_widget_modifier_indicator_obj(&modifier_indicator_widget), 25, 8);

    zmk_widget_layer_name_init(&layer_name_widget, screen);
    lv_obj_set_pos(zmk_widget_layer_name_obj(&layer_name_widget), 10, 42);

    zmk_widget_layer_display_init(&layer_display_widget, screen);
    lv_obj_set_pos(zmk_widget_layer_display_obj(&layer_display_widget), 10, 142);

    // Bottom row: left battery | output | right battery, spaced evenly across
    // the full 260px width (space-evenly). battery_circles holds the two arcs
    // (one gap in from each edge); the output widget sits in the centre gap.
    // gap = 7, so output x = 10 (row left) + 7 (gap) + 58 (arc) + 7 (gap) = 82.
    zmk_widget_battery_circles_init(&battery_circles_widget, screen);
    lv_obj_set_pos(zmk_widget_battery_circles_obj(&battery_circles_widget), 10, 170);

    zmk_widget_output_init(&output_widget, screen);
    lv_obj_set_pos(zmk_widget_output_obj(&output_widget), 82, 170);

    return screen;
}
