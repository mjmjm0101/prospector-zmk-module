#include <zephyr/sys/util.h> /* IS_ENABLED, MIN */

#include "output_menu.h"

#if IS_ENABLED(CONFIG_PROSPECTOR_TOUCH_OUTPUT_MENU)

#include <stdint.h>
#include <stdio.h>
#include <lvgl.h>

#include <zephyr/kernel.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(output_menu, 4);

#include "layouts/operator/display_colors.h"

/*
 * Long-press the lit status screen to open a modal output-select menu:
 *
 *   OUTPUT
 *   [   USB   ]
 *   [ BT1 ] [ BT2 ]
 *   [ BT3 ] [ BT4 ]
 *   [  CLEAR  ]          <- only when the active BLE profile is paired
 *
 * Tapping USB/BTn switches the endpoint and closes the menu (select-to-close).
 * CLEAR opens a two-step YES/NO confirmation before unpairing the active
 * profile. The menu also auto-closes after a few seconds of inactivity.
 *
 * Labels are ASCII only: the operator layout's default font has no Japanese
 * glyphs.
 *
 * The button callbacks run on the LVGL/display thread. LVGL object mutations
 * stay there, but the endpoint/BLE changes (transport switch, profile select,
 * bond clear) are handed to the system work queue via endpoint_work so they run
 * in the same context ZMK normally drives them from, rather than from the
 * display thread.
 */

#define BT_PROFILE_COUNT MIN(4, ZMK_BLE_PROFILE_COUNT)
#define MENU_AUTO_CLOSE_MS 8000

/* Action ids passed as the event user_data (encoded as a pointer). */
enum menu_action {
    ACTION_USB = 1,
    ACTION_BT_BASE,                  /* ACTION_BT_BASE + i selects BLE profile i */
    ACTION_CLEAR = ACTION_BT_BASE + 16,
    ACTION_CONFIRM_YES,
    ACTION_CONFIRM_NO,
};

static lv_obj_t *s_catcher;
static lv_obj_t *s_overlay;
static lv_timer_t *s_close_timer;

static void close_menu(void);
static void build_menu(void);
static void build_confirm(void);
static void on_click(lv_event_t *e);

/* Endpoint/BLE changes are deferred off the display thread onto the system
 * work queue. Clicks are serialised on the display thread, so a single pending
 * slot is enough. */
enum endpoint_op { OP_NONE = 0, OP_USB, OP_BT, OP_CLEAR };
static enum endpoint_op pending_op;
static uint8_t pending_bt_index;

static void endpoint_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    switch (pending_op) {
    case OP_USB:
        zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB);
        break;
    case OP_BT:
        zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_BLE);
        zmk_ble_prof_select(pending_bt_index);
        break;
    case OP_CLEAR:
        zmk_ble_clear_bonds(); /* clears the active profile, then re-advertises */
        break;
    default:
        break;
    }
    pending_op = OP_NONE;
}

static K_WORK_DEFINE(endpoint_work, endpoint_work_cb);

static void auto_close_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    close_menu();
}

static void arm_auto_close(void) {
    if (s_close_timer) {
        lv_timer_reset(s_close_timer);
    }
}

/* A flat "button": a clickable container with a centered label. LV_USE_BUTTON
 * is not enabled in this build, so we style a plain lv_obj instead. */
static void make_button(lv_obj_t *parent, const char *text, int width_pct, uint32_t bg,
                        uint32_t text_color, bool highlighted, enum menu_action action) {
    lv_color_t base = lv_color_hex(bg);

    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, lv_pct(width_pct), 52);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, highlighted ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, base, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    /* Lighten the fill while the button is held, for press feedback. */
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), base, 80),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, (void *)(intptr_t)action);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(text_color), LV_PART_MAIN);
    lv_obj_center(label);
}

/* Single click handler for every button; routed by the action user_data. */
static void on_click(lv_event_t *e) {
    enum menu_action action = (enum menu_action)(intptr_t)lv_event_get_user_data(e);

    arm_auto_close();

    if (action == ACTION_USB) {
        pending_op = OP_USB;
        k_work_submit(&endpoint_work);
        close_menu();
        return;
    }
    if (action >= ACTION_BT_BASE && action < ACTION_BT_BASE + BT_PROFILE_COUNT) {
        pending_bt_index = action - ACTION_BT_BASE;
        pending_op = OP_BT;
        k_work_submit(&endpoint_work);
        close_menu();
        return;
    }
    if (action == ACTION_CLEAR) {
        build_confirm();
        return;
    }
    if (action == ACTION_CONFIRM_YES) {
        /* CLEAR is only offered for the active (selected) profile, so clearing
         * the active profile's bond is correct. */
        pending_op = OP_CLEAR;
        k_work_submit(&endpoint_work);
        close_menu();
        return;
    }
    if (action == ACTION_CONFIRM_NO) {
        build_menu(); /* back to the main menu */
        return;
    }
}

static void add_title(const char *text) {
    lv_obj_t *title = lv_label_create(s_overlay);
    lv_label_set_text(title, text);
    lv_obj_set_width(title, lv_pct(100)); /* own row in the wrapping flex */
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN);
}

static void build_menu(void) {
    lv_obj_clean(s_overlay);

    struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
    bool is_usb = (selected.transport == ZMK_TRANSPORT_USB);
    int active_ble = zmk_ble_active_profile_index();

    make_button(s_overlay, "USB", 92,
                is_usb ? DISPLAY_COLOR_USB_ACTIVE_BG : DISPLAY_COLOR_USB_INACTIVE_BG,
                is_usb ? DISPLAY_COLOR_OUTPUT_ACTIVE_TEXT : DISPLAY_COLOR_OUTPUT_INACTIVE_TEXT,
                is_usb, ACTION_USB);

    for (int i = 0; i < BT_PROFILE_COUNT; i++) {
        bool active = (!is_usb && i == active_ble);
        char label[8];
        snprintf(label, sizeof(label), "BT%d", i + 1);
        make_button(s_overlay, label, 44,
                    active ? DISPLAY_COLOR_BLE_ACTIVE_BG : DISPLAY_COLOR_BLE_INACTIVE_BG,
                    active ? DISPLAY_COLOR_OUTPUT_ACTIVE_TEXT : DISPLAY_COLOR_OUTPUT_INACTIVE_TEXT,
                    active, (enum menu_action)(ACTION_BT_BASE + i));
    }

    /* CLEAR only when the active output is a paired BLE profile. */
    if (!is_usb && active_ble >= 0 && !zmk_ble_profile_is_open((uint8_t)active_ble)) {
        make_button(s_overlay, "CLEAR", 92, DISPLAY_COLOR_WPM_BAR_ACTIVE, 0xffffff, false,
                    ACTION_CLEAR);
    }
}

static void build_confirm(void) {
    lv_obj_clean(s_overlay);

    int active_ble = zmk_ble_active_profile_index();
    char msg[16];
    snprintf(msg, sizeof(msg), "CLEAR BT%d?", active_ble + 1);

    add_title(msg);

    make_button(s_overlay, "YES", 44, DISPLAY_COLOR_WPM_BAR_ACTIVE, 0xffffff, false,
                ACTION_CONFIRM_YES);
    make_button(s_overlay, "NO", 44, DISPLAY_COLOR_SLOT_INACTIVE_BG, 0xffffff, false,
                ACTION_CONFIRM_NO);
}

static void close_menu(void) {
    if (s_close_timer) {
        lv_timer_delete(s_close_timer);
        s_close_timer = NULL;
    }
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
}

static void open_menu(void) {
    if (s_overlay) {
        return;
    }
    lv_obj_t *screen = lv_screen_active();

    s_overlay = lv_obj_create(screen);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay, 6, LV_PART_MAIN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE); /* absorb taps behind it */

    /* Two-per-row wrapping layout; full-width buttons get their own row. */
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_overlay, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_overlay, 6, LV_PART_MAIN);

    lv_obj_move_foreground(s_overlay);

    build_menu();

    s_close_timer = lv_timer_create(auto_close_cb, MENU_AUTO_CLOSE_MS, NULL);
}

static void on_long_press(lv_event_t *e) {
    ARG_UNUSED(e);
    open_menu();
}

void prospector_output_menu_attach(lv_obj_t *screen) {
    if (!screen || s_catcher) {
        return;
    }
    /* Transparent, full-screen, top-most catcher for the long-press gesture.
     * The status widgets below are non-interactive, so intercepting taps here
     * is harmless. */
    s_catcher = lv_obj_create(screen);
    lv_obj_set_size(s_catcher, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_catcher, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_catcher, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_catcher, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_catcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_catcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_catcher, on_long_press, LV_EVENT_LONG_PRESSED, NULL);
}

#else /* !CONFIG_PROSPECTOR_TOUCH_OUTPUT_MENU */

void prospector_output_menu_attach(lv_obj_t *screen) { ARG_UNUSED(screen); }

#endif
