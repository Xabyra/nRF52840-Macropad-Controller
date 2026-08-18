/** @file
 *  @brief Application Event System
 *  
 *  Centralized event queue for event-driven architecture.
 *  All events from BLE, GPIO, timers, etc. are posted here,
 *  and consumed by the app state machine.
 */

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Application Event Types
 */
enum app_event_type {
    /* System boot */
    APP_EVT_BOOT,

    /* Bluetooth events */
    APP_EVT_BT_READY,
    APP_EVT_BLE_CONNECTED,
    APP_EVT_BLE_DISCONNECTED,
    APP_EVT_BLE_SECURITY_CHANGED,
    APP_EVT_BLE_HID_READY,
    APP_EVT_BLE_SECURITY_FAILED,

    /* Input events - keyboard matrix */
    APP_EVT_KEY_PRESSED,
    APP_EVT_KEY_RELEASED,

    /* Input events - encoder */
    APP_EVT_ENCODER_CW,
    APP_EVT_ENCODER_CCW,
    APP_EVT_ENCODER_DELTA,
    APP_EVT_ENCODER_BUTTON_PRESSED,
    APP_EVT_ENCODER_BUTTON_RELEASED,

    /* Timers and periodic events */
    APP_EVT_INACTIVITY_TIMEOUT,
    APP_EVT_ADVERTISING_RESTART,
    APP_EVT_BATTERY_TICK,

    /* Configuration events */
    APP_EVT_PROFILE_CHANGED,
    APP_EVT_BACKLIGHT_CHANGED,
    APP_EVT_SLEEP_TIMEOUT_CHANGED,
    APP_EVT_SEQUENCE_RUN_REQUESTED,

    /* User action events */
    APP_EVT_CLEAR_BONDS_REQUESTED,

    /* Power management events */
    APP_EVT_PREPARE_SLEEP,
    APP_EVT_ENTER_SYSTEM_OFF,

    /* Error events */
    APP_EVT_ERROR,
};

/**
 * @brief Application Event Structure
 * 
 * Contains event type and optional event-specific data.
 */
struct app_event {
    enum app_event_type type;

    union {
        /* For key press/release: row and column */
        struct {
            uint8_t row;
            uint8_t col;
        } key;

        /* For BLE events: reason code */
        struct {
            uint8_t reason;
        } ble;

        /* For profile/config: index or value */
        struct {
            uint8_t profile_index;
        } profile;

        /* Encoder delta (signed rotation amount) */
        struct {
            int8_t delta;
        } encoder;

        /* Generic 8-bit value */
        struct {
            uint8_t value;
        } u8;

        /* Error code */
        int err;
    } data;
};

/**
 * @brief Post a complete event to the queue
 * 
 * @param evt Pointer to the event structure
 */
void app_post_event(const struct app_event *evt);

/**
 * @brief Post a simple event (no additional data)
 * 
 * @param type Event type
 */
void app_post_simple(enum app_event_type type);

/**
 * @brief Post a key event (press or release)
 * 
 * @param type APP_EVT_KEY_PRESSED or APP_EVT_KEY_RELEASED
 * @param row Matrix row index
 * @param col Matrix column index
 */
void app_post_key(enum app_event_type type, uint8_t row, uint8_t col);

/**
 * @brief Post an event with 8-bit value
 * 
 * @param type Event type
 * @param value 8-bit value
 */
void app_post_u8(enum app_event_type type, uint8_t value);

/**
 * @brief Post an encoder delta event
 * 
 * @param delta Signed encoder delta
 */
void app_post_encoder_delta(int delta);

/**
 * @brief Post a BLE security failed event
 * 
 * @param err BLE security error code
 */
void app_post_security_failed(int err);

/**
 * @brief Post an error event
 * 
 * @param err Error code
 */
void app_post_error(int err);

/**
 * @brief Drop all newly posted non-critical events.
 *
 * APP_EVT_ERROR is still accepted so fatal failures can be observed.
 */
void app_events_block_noncritical(bool blocked);

/**
 * @brief Wait for an event from the queue
 * 
 * Blocks until an event is available.
 * 
 * @param evt Pointer to store received event
 * @return 0 on success, error code otherwise
 */
int app_wait_event(struct app_event *evt);

/**
 * @brief Get human-readable name for event type
 * 
 * Useful for debug logging.
 * 
 * @param type Event type
 * @return String name of the event type
 */
const char *app_event_type_name(enum app_event_type type);

#endif /* APP_EVENTS_H */
