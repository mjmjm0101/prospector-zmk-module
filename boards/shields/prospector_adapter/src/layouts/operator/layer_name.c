#include "layer_name.h"

#include <ctype.h>
#include <string.h>

#include <zmk/display.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include <fonts.h>
#include "display_colors.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// Widest the layer name may render. The widget is 260px wide; leaving ~18px
// on each side keeps a little breathing room (the requested left/right margin).
#define LAYER_NAME_MAX_WIDTH 224

// Font candidates from largest to smallest. The largest font whose WIDEST
// layer name still fits within LAYER_NAME_MAX_WIDTH is used for every layer,
// so the size is chosen for the longest name (e.g. "SIGN&CALC") and stays
// consistent across layers.
static const lv_font_t *const FONT_LADDER[] = {
    &FR_Regular_48,
    &FR_Regular_36,
    &FR_Medium_32,
    &FR_Regular_30,
};
#define FONT_LADDER_LEN (sizeof(FONT_LADDER) / sizeof(FONT_LADDER[0]))

struct layer_name_state {
    uint8_t index;
};

static void resolve_layer_name(uint8_t index, char *out, size_t out_len) {
    const char *layer_name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));

    if (layer_name && *layer_name) {
        snprintf(out, out_len, "%s", layer_name);
    } else {
        snprintf(out, out_len, "Layer %d", index);
    }

#if IS_ENABLED(CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE)
    for (int i = 0; out[i]; i++) {
        out[i] = toupper((unsigned char)out[i]);
    }
#endif
}

// Pick the largest font whose widest layer name fits the available width.
static const lv_font_t *pick_layer_font(void) {
    for (size_t f = 0; f < FONT_LADDER_LEN; f++) {
        const lv_font_t *font = FONT_LADDER[f];
        int32_t widest = 0;

        for (uint8_t i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
            char name[32];
            resolve_layer_name(i, name, sizeof(name));
            int32_t w = lv_text_get_width(name, strlen(name), font, 0);
            if (w > widest) {
                widest = w;
            }
        }

        if (widest <= LAYER_NAME_MAX_WIDTH) {
            return font;
        }
    }

    return FONT_LADDER[FONT_LADDER_LEN - 1];
}

static void layer_name_update_cb(struct layer_name_state state) {
    char display_name[32];
    resolve_layer_name(state.index, display_name, sizeof(display_name));

    struct zmk_widget_layer_name *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->label, display_name);
        lv_obj_center(widget->label);
    }
}

static struct layer_name_state layer_name_get_state(const zmk_event_t *eh) {
    return (struct layer_name_state){.index = zmk_keymap_highest_layer_active()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_name, struct layer_name_state,
                            layer_name_update_cb, layer_name_get_state)
ZMK_SUBSCRIPTION(widget_layer_name, zmk_layer_state_changed);

int zmk_widget_layer_name_init(struct zmk_widget_layer_name *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 260, 90);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);

    widget->label = lv_label_create(widget->obj);
    lv_label_set_long_mode(widget->label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(widget->label, pick_layer_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(DISPLAY_COLOR_LAYER_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(widget->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    char display_name[32];
    resolve_layer_name(zmk_keymap_highest_layer_active(), display_name, sizeof(display_name));
    lv_label_set_text(widget->label, display_name);
    lv_obj_center(widget->label);

    sys_slist_append(&widgets, &widget->node);
    widget_layer_name_init();

    return 0;
}

lv_obj_t *zmk_widget_layer_name_obj(struct zmk_widget_layer_name *widget) {
    return widget->obj;
}
