#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(idle_dim, 4);

/*
 * Backlight idle dimming for the Prospector dongle display.
 *
 * The dongle is mains/USB powered and stays awake, so we dim and then blank the
 * backlight after a period without input instead of relying on ZMK's single
 * activity-idle step. Any key event from the split halves arrives here as a
 * zmk_position_state_changed event and restores full brightness.
 *
 * Only used in fixed-brightness mode; with the ambient light sensor enabled the
 * als_thread in brightness.c owns the backlight instead.
 */
#ifndef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR

#define BRIGHT_NORMAL CONFIG_PROSPECTOR_FIXED_BRIGHTNESS
#define BRIGHT_DIM    40
#define BRIGHT_OFF    0

#define DIM_DELAY_MS  30000
#define OFF_DELAY_MS  60000

static const struct device *pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);
#define DISP_BL DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

static void set_bl(uint8_t pct) {
    if (led_set_brightness(pwm_leds_dev, DISP_BL, pct)) {
        LOG_ERR("Failed to set backlight to %d%%", pct);
    }
}

static void dim_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    set_bl(BRIGHT_DIM);
}

static void off_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    set_bl(BRIGHT_OFF);
}

static K_WORK_DELAYABLE_DEFINE(dim_work, dim_work_cb);
static K_WORK_DELAYABLE_DEFINE(off_work, off_work_cb);

static void arm_timers(void) {
    k_work_reschedule(&dim_work, K_MSEC(DIM_DELAY_MS));
    k_work_reschedule(&off_work, K_MSEC(OFF_DELAY_MS));
}

static int activity_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    set_bl(BRIGHT_NORMAL);
    arm_timers();
    return 0;
}

ZMK_LISTENER(prospector_idle_dim, activity_listener);
ZMK_SUBSCRIPTION(prospector_idle_dim, zmk_position_state_changed);

static int idle_dim_init(void) {
    arm_timers();
    return 0;
}

SYS_INIT(idle_dim_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* !CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR */
