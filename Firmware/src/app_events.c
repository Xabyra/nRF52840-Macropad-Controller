/** @file
 *  @brief Application Event System Implementation
 */

#include "app_events.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* Define event message queue: 32 messages of event size, 4-byte alignment */
K_MSGQ_DEFINE(app_event_msgq, sizeof(struct app_event), 32, 4);

static atomic_t block_noncritical_events;

void app_events_block_noncritical(bool blocked)
{
    atomic_set(&block_noncritical_events, blocked ? 1 : 0);
}

void app_post_event(const struct app_event *evt)
{
    if (!evt) {
        printk("APP: Error - NULL event\n");
        return;
    }

    if (atomic_get(&block_noncritical_events) && evt->type != APP_EVT_ERROR) {
        return;
    }

    int err = k_msgq_put(&app_event_msgq, (void *)evt, K_NO_WAIT);
    if (err) {
        printk("APP: Event queue full (type %d), dropped event\n", evt->type);
    }
}

void app_post_simple(enum app_event_type type)
{
    struct app_event evt = {
        .type = type,
        .data.u8 = {0}
    };
    app_post_event(&evt);
}

void app_post_key(enum app_event_type type, uint8_t row, uint8_t col)
{
    struct app_event evt = {
        .type = type,
        .data = {
            .key = {
                .row = row,
                .col = col,
            }
        }
    };
    app_post_event(&evt);
}

void app_post_u8(enum app_event_type type, uint8_t value)
{
    struct app_event evt = {
        .type = type,
        .data = {
            .u8 = {
                .value = value,
            }
        }
    };
    app_post_event(&evt);
}

void app_post_encoder_delta(int delta)
{
    if (delta == 0) {
        return;
    }

    if (delta > 127) {
        delta = 127;
    } else if (delta < -127) {
        delta = -127;
    }

    struct app_event evt = {
        .type = APP_EVT_ENCODER_DELTA,
        .data = {
            .encoder = {
                .delta = delta,
            }
        }
    };
    app_post_event(&evt);
}

void app_post_security_failed(int err)
{
    struct app_event evt = {
        .type = APP_EVT_BLE_SECURITY_FAILED,
        .data = {
            .err = err,
        }
    };
    app_post_event(&evt);
}

void app_post_error(int err)
{
    struct app_event evt = {
        .type = APP_EVT_ERROR,
        .data = {
            .err = err,
        }
    };
    app_post_event(&evt);
}

int app_wait_event(struct app_event *evt)
{
    if (!evt) {
        return -EINVAL;
    }

    int err = k_msgq_get(&app_event_msgq, evt, K_FOREVER);
    return err;
}

/**
 * @brief Get human-readable name for event type (for debug logging)
 */
const char *app_event_type_name(enum app_event_type type)
{
    switch (type) {
    case APP_EVT_BOOT:                   return "BOOT";
    case APP_EVT_BT_READY:               return "BT_READY";
    case APP_EVT_BLE_CONNECTED:          return "BLE_CONNECTED";
    case APP_EVT_BLE_DISCONNECTED:       return "BLE_DISCONNECTED";
    case APP_EVT_BLE_SECURITY_CHANGED:   return "BLE_SECURITY_CHANGED";
    case APP_EVT_BLE_HID_READY:          return "BLE_HID_READY";
    case APP_EVT_BLE_SECURITY_FAILED:    return "BLE_SECURITY_FAILED";
    case APP_EVT_KEY_PRESSED:            return "KEY_PRESSED";
    case APP_EVT_KEY_RELEASED:           return "KEY_RELEASED";
    case APP_EVT_ENCODER_CW:             return "ENCODER_CW";
    case APP_EVT_ENCODER_CCW:            return "ENCODER_CCW";
    case APP_EVT_ENCODER_DELTA:          return "ENCODER_DELTA";
    case APP_EVT_ENCODER_BUTTON_PRESSED: return "ENCODER_BUTTON_PRESSED";
    case APP_EVT_ENCODER_BUTTON_RELEASED:return "ENCODER_BUTTON_RELEASED";
    case APP_EVT_INACTIVITY_TIMEOUT:     return "INACTIVITY_TIMEOUT";
    case APP_EVT_ADVERTISING_RESTART:    return "ADVERTISING_RESTART";
    case APP_EVT_BATTERY_TICK:           return "BATTERY_TICK";
    case APP_EVT_PROFILE_CHANGED:        return "PROFILE_CHANGED";
    case APP_EVT_BACKLIGHT_CHANGED:      return "BACKLIGHT_CHANGED";
    case APP_EVT_SLEEP_TIMEOUT_CHANGED:  return "SLEEP_TIMEOUT_CHANGED";
    case APP_EVT_SEQUENCE_RUN_REQUESTED: return "SEQUENCE_RUN_REQUESTED";
    case APP_EVT_CLEAR_BONDS_REQUESTED:  return "CLEAR_BONDS_REQUESTED";
    case APP_EVT_PREPARE_SLEEP:          return "PREPARE_SLEEP";
    case APP_EVT_ENTER_SYSTEM_OFF:       return "ENTER_SYSTEM_OFF";
    case APP_EVT_ERROR:                  return "ERROR";
    default:                             return "UNKNOWN";
    }
}
