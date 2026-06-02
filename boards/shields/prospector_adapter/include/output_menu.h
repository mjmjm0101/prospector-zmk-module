#pragma once

#include <lvgl.h>

/*
 * Touch output-select menu for the Prospector dongle.
 *
 * Attaches a full-screen, transparent long-press catcher to the given status
 * screen. Long-pressing the lit display opens a modal menu to pick the output
 * endpoint (USB / BT1-BT4) and, when the active BLE profile is paired, to clear
 * (unpair) it behind a two-step confirmation.
 *
 * No-op unless CONFIG_PROSPECTOR_TOUCH_OUTPUT_MENU is enabled.
 */
void prospector_output_menu_attach(lv_obj_t *screen);
