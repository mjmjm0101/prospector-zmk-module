/*
 * Stuck key watchdog for the dongle (split central).
 *
 * Non-modifier HID keycodes that stay "pressed" longer than
 * CONFIG_PROSPECTOR_STUCK_KEY_TIMEOUT_MS get a synthetic release. Layer keys
 * never produce keycode events, and modifier usages (0xE0-0xE7) are skipped, so
 * holds of Shift/Ctrl/layers keep working. A later genuine release for the same
 * key is harmless (double release is idempotent in the HID layer).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TIMEOUT_MS CONFIG_PROSPECTOR_STUCK_KEY_TIMEOUT_MS
#define SLOTS 16

struct held_key {
    bool used;
    uint16_t usage_page;
    uint32_t keycode;
    uint8_t implicit_modifiers;
    int64_t pressed_at;
};

static struct held_key held[SLOTS];
static struct k_spinlock lock;
static void check_held(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(check_work, check_held);

static int find_slot(uint16_t page, uint32_t keycode) {
    for (int i = 0; i < SLOTS; i++) {
        if (held[i].used && held[i].usage_page == page && held[i].keycode == keycode) {
            return i;
        }
    }
    return -1;
}

static void check_held(struct k_work *work) {
    struct held_key stuck[SLOTS];
    int n = 0;
    int64_t now = k_uptime_get();
    int64_t next = -1;

    k_spinlock_key_t k = k_spin_lock(&lock);
    for (int i = 0; i < SLOTS; i++) {
        if (!held[i].used) {
            continue;
        }
        int64_t age = now - held[i].pressed_at;
        if (age >= TIMEOUT_MS) {
            stuck[n++] = held[i];
            held[i].used = false;
        } else {
            int64_t remain = TIMEOUT_MS - age;
            if (next < 0 || remain < next) {
                next = remain;
            }
        }
    }
    k_spin_unlock(&lock, k);

    for (int i = 0; i < n; i++) {
        LOG_WRN("stuck key: page 0x%02x code 0x%02x held %dms, forcing release", stuck[i].usage_page,
                stuck[i].keycode, TIMEOUT_MS);
        raise_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
            .usage_page = stuck[i].usage_page,
            .keycode = stuck[i].keycode,
            .implicit_modifiers = stuck[i].implicit_modifiers,
            .explicit_modifiers = 0,
            .state = false,
            .timestamp = now,
        });
    }

    if (next >= 0) {
        k_work_reschedule(&check_work, K_MSEC(next));
    }
}

static int stuck_key_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || is_mod(ev->usage_page, ev->keycode)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool schedule = false;
    k_spinlock_key_t k = k_spin_lock(&lock);
    int idx = find_slot(ev->usage_page, ev->keycode);
    if (ev->state) {
        if (idx < 0) {
            for (int i = 0; i < SLOTS; i++) {
                if (!held[i].used) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx >= 0) {
            held[idx] = (struct held_key){
                .used = true,
                .usage_page = ev->usage_page,
                .keycode = ev->keycode,
                .implicit_modifiers = ev->implicit_modifiers,
                .pressed_at = k_uptime_get(),
            };
            schedule = true;
        }
    } else if (idx >= 0) {
        held[idx].used = false;
    }
    k_spin_unlock(&lock, k);

    if (schedule) {
        /* Only (re)schedule if nothing earlier is pending. */
        k_ticks_t remaining = k_work_delayable_remaining_get(&check_work);
        if (remaining == 0 || k_ticks_to_ms_floor64(remaining) > TIMEOUT_MS) {
            k_work_schedule(&check_work, K_MSEC(TIMEOUT_MS));
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(stuck_key_release, stuck_key_listener);
ZMK_SUBSCRIPTION(stuck_key_release, zmk_keycode_state_changed);
