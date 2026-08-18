/** @file
 *  @brief Application State Machine Implementation
 */

#include "app_sm.h"
#include "app_events.h"
#include "hog.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>

extern int ble_app_start_advertising(void);
extern int ble_app_stop_advertising(void);
extern uint8_t ble_app_active_connections(void);
extern void ble_app_disconnect_all_for_sleep(void);
extern int ble_app_clear_bonds(void);

#define DEBUG_COMBO_LOG 0
#define DEBUG_ENCODER_LOG 0

/**
 * @brief Application States
 */
enum app_state {
    APP_STATE_BOOT,            /* Initial state */
    APP_STATE_INIT_HW,         /* Initializing hardware */
    APP_STATE_INIT_BT,         /* Waiting for BT to be ready */
    APP_STATE_ADVERTISING,     /* Advertising and ready for connection */
    APP_STATE_LOW_POWER_CONNECTED, /* Connected with backlight off */
    APP_STATE_ACTIVE,          /* Active input processing */
    APP_STATE_PREPARE_SLEEP,   /* Preparing for system off */
    APP_STATE_SYSTEM_OFF,      /* System off (should not return) */
    APP_STATE_ERROR,           /* Error state */
};

static enum app_state current_state = APP_STATE_BOOT;
static bool sleep_in_progress = false;
static bool pending_clear_bonds = false;
static uint8_t reconnect_fail_count = 0;

static void advertising_restart_work_handler(struct k_work *work);
static void cancel_advertising_restart(void);
static K_WORK_DELAYABLE_DEFINE(advertising_restart_work, advertising_restart_work_handler);

/**
 * @brief Helper to get state name for logging
 */
static const char *state_name(enum app_state s)
{
    switch (s) {
    case APP_STATE_BOOT:           return "BOOT";
    case APP_STATE_INIT_HW:        return "INIT_HW";
    case APP_STATE_INIT_BT:        return "INIT_BT";
    case APP_STATE_ADVERTISING:    return "ADVERTISING";
    case APP_STATE_LOW_POWER_CONNECTED: return "LOW_POWER_CONNECTED";
    case APP_STATE_ACTIVE:         return "ACTIVE";
    case APP_STATE_PREPARE_SLEEP:  return "PREPARE_SLEEP";
    case APP_STATE_SYSTEM_OFF:     return "SYSTEM_OFF";
    case APP_STATE_ERROR:          return "ERROR";
    default:                        return "UNKNOWN";
    }
}

const char *app_sm_get_state_name(void)
{
    return state_name(current_state);
}

/**
 * @brief Change state with logging
 */
static void change_state(enum app_state new_state)
{
    if (new_state == current_state) {
        return;
    }

    printk("APP: state %s -> %s\n", state_name(current_state), state_name(new_state));
    current_state = new_state;
}

/**
 * @brief Called when entering a state
 */
static void on_enter(enum app_state state)
{
    switch (state) {
    case APP_STATE_BOOT:
        /* Boot just posts another event */
        break;

    case APP_STATE_INIT_HW:
        printk("APP: Initializing hardware...\n");
        hog_init();
        input_scan_init();
        inactivity_timer_init();
        hog_button_loop();
        input_scan_set_enabled(true);
        /* Automatically transition to INIT_BT after initialization */
        change_state(APP_STATE_INIT_BT);
        break;

    case APP_STATE_INIT_BT:
        printk("APP: Waiting for Bluetooth ready...\n");
        /* Transition happens via APP_EVT_BT_READY */
        break;

    case APP_STATE_ADVERTISING:
        printk("APP: Starting BLE advertising\n");
        input_scan_set_enabled(true);
        ble_app_start_advertising();
        inactivity_timer_reset();
        break;

    case APP_STATE_LOW_POWER_CONNECTED:
        printk("APP: Connected low-power mode\n");
        input_scan_set_enabled(true);
        input_enter_low_power_connected();
        inactivity_timer_reset_system_off_timeout();
        break;

    case APP_STATE_ACTIVE:
        printk("APP: Active input processing\n");
        input_scan_set_enabled(true);
        inactivity_timer_reset();
        break;

    case APP_STATE_PREPARE_SLEEP:
        printk("APP: Preparing for sleep...\n");
        sleep_in_progress = true;
        app_events_block_noncritical(true);
        cancel_advertising_restart();
        inactivity_timer_stop();
        input_scan_set_enabled(false);
        input_disable_runtime_interrupts();
        ble_app_disconnect_all_for_sleep();
        power_prepare_system_off();
        printk("APP: entering real SYSTEM OFF now\n");
        printk("APP: state %s -> %s\n", state_name(current_state), state_name(APP_STATE_SYSTEM_OFF));
        current_state = APP_STATE_SYSTEM_OFF;
        power_enter_system_off();
        /* If we return, system off failed, reboot to recover */
        printk("APP ERROR: power_enter_system_off returned, rebooting\n");
#ifdef CONFIG_REBOOT
        sys_reboot(SYS_REBOOT_COLD);
#else
        NVIC_SystemReset();
#endif
        break;

    case APP_STATE_SYSTEM_OFF:
        printk("APP: Entering system off\n");
        sleep_in_progress = true;
        app_events_block_noncritical(true);
        power_enter_system_off();
        /* Should never return from system_off */
        break;

    case APP_STATE_ERROR:
        printk("APP: ERROR state\n");
        break;

    default:
        break;
    }
}

static void cancel_advertising_restart(void)
{
    k_work_cancel_delayable(&advertising_restart_work);
}

static void advertising_restart_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (sleep_in_progress) {
        return;
    }

    app_post_simple(APP_EVT_ADVERTISING_RESTART);
}

static void schedule_advertising_restart(uint8_t reason)
{
    if (sleep_in_progress) {
        return;
    }

    reconnect_fail_count++;
    uint32_t delay_ms = reconnect_fail_count * 1000U;

    if (delay_ms > 10000U) {
        delay_ms = 10000U;
    }

    if (reconnect_fail_count >= 5) {
        printk("APP: too many reconnect failures; bonds likely stale. Use clear-bonds combo and remove host device.\n");
    }

    printk("APP: scheduling advertising restart %u ms after disconnect reason 0x%02x\n",
           delay_ms, reason);

    ble_app_stop_advertising();
    change_state(APP_STATE_ADVERTISING);
    input_scan_set_enabled(true);
    inactivity_timer_reset();
    k_work_reschedule(&advertising_restart_work, K_MSEC(delay_ms));
}

/**
 * @brief Called when exiting a state (if needed for cleanup)
 */
__attribute__((unused))
static void on_exit(enum app_state state)
{
    (void)state;
    /* Most states don't need exit logic for now */
}

/**
 * @brief Handle input events (key press/release, encoder)
 */
/**
 * @brief Check and handle clear bonds combo (encoder button + 2+ matrix keys)
 * 
 * This helper is called after input events to detect the combo.
 * If detected, it clears BLE bonding records and restarts advertising.
 */
static bool app_check_clear_bonds_combo(void)
{
#if DEBUG_COMBO_LOG
    printk("APP: combo check enc=%d keys=%u done=%d\n",
           input_encoder_button_is_pressed(),
           input_keyboard_pressed_count(),
           input_combo_unpair_already_done());
#endif

    if (input_combo_unpair_condition()) {
        if (!input_combo_unpair_already_done()) {
            printk("APP: Clear bonds combo detected\n");
            input_combo_unpair_set_done(true);
            int err = ble_app_clear_bonds();
            if (err) {
                input_combo_unpair_set_done(false);
            }
        }
        return true;
    }

    if (input_combo_unpair_already_done()) {
        printk("APP: Clear bonds combo latch reset\n");
    }
    input_combo_unpair_set_done(false);
    return false;
}

static void app_handle_input_event_common(const struct app_event *evt)
{
    enum app_event_type type = evt->type;

    if (current_state == APP_STATE_LOW_POWER_CONNECTED &&
        (type == APP_EVT_KEY_PRESSED ||
         type == APP_EVT_ENCODER_CW ||
         type == APP_EVT_ENCODER_CCW ||
         type == APP_EVT_ENCODER_DELTA ||
         type == APP_EVT_ENCODER_BUTTON_PRESSED)) {
        input_exit_low_power_connected();
        change_state(APP_STATE_ACTIVE);
    }

    inactivity_timer_reset();

    if (app_check_clear_bonds_combo()) {
        return;
    }

    switch (type) {
    case APP_EVT_KEY_PRESSED:
        printk("APP: Key pressed [%d,%d]\n", evt->data.key.row, evt->data.key.col);
        input_handle_key_pressed(evt->data.key.row, evt->data.key.col);
        hid_send_current_input_state();
        if (ble_app_active_connections() > 0 &&
            (current_state == APP_STATE_ADVERTISING ||
             current_state == APP_STATE_LOW_POWER_CONNECTED)) {
            change_state(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_KEY_RELEASED:
        printk("APP: Key released [%d,%d]\n", evt->data.key.row, evt->data.key.col);
        hid_send_current_input_state();
        break;

    case APP_EVT_ENCODER_CW:
#if DEBUG_ENCODER_LOG
        printk("APP: ENCODER_CW\n");
#endif
        input_execute_encoder_action(true);
        if (ble_app_active_connections() > 0 &&
            (current_state == APP_STATE_ADVERTISING ||
             current_state == APP_STATE_LOW_POWER_CONNECTED)) {
            change_state(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_ENCODER_CCW:
#if DEBUG_ENCODER_LOG
        printk("APP: ENCODER_CCW\n");
#endif
        input_execute_encoder_action(false);
        if (ble_app_active_connections() > 0 &&
            (current_state == APP_STATE_ADVERTISING ||
             current_state == APP_STATE_LOW_POWER_CONNECTED)) {
            change_state(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_ENCODER_DELTA:
#if DEBUG_ENCODER_LOG
        printk("APP: ENCODER_DELTA %d\n", evt->data.encoder.delta);
#endif
        input_execute_encoder_delta(evt->data.encoder.delta);
        if (ble_app_active_connections() > 0 &&
            (current_state == APP_STATE_ADVERTISING ||
             current_state == APP_STATE_LOW_POWER_CONNECTED)) {
            change_state(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_ENCODER_BUTTON_PRESSED:
        printk("APP: Encoder button pressed\n");
        input_handle_encoder_button_event(type);
        if (ble_app_active_connections() > 0 &&
            (current_state == APP_STATE_ADVERTISING ||
             current_state == APP_STATE_LOW_POWER_CONNECTED)) {
            change_state(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_ENCODER_BUTTON_RELEASED:
        printk("APP: Encoder button released\n");
        input_handle_encoder_button_event(type);
        break;

    default:
        break;
    }
}

/**
 * @brief Main event handler
 */
static void handle_event(const struct app_event *evt)
{
    enum app_event_type type = evt->type;

#if !DEBUG_ENCODER_LOG
    if (type != APP_EVT_ENCODER_CW && type != APP_EVT_ENCODER_CCW) {
        printk("APP: event %s in state %s\n", app_event_type_name(type), state_name(current_state));
    }
#else
    printk("APP: event %s in state %s\n", app_event_type_name(type), state_name(current_state));
#endif

    if (current_state == APP_STATE_SYSTEM_OFF) {
        printk("APP: ignoring event %s in SYSTEM_OFF\n", app_event_type_name(type));
        return;
    }

    if (current_state == APP_STATE_PREPARE_SLEEP) {
        if (type == APP_EVT_ERROR) {
            printk("APP: critical error while preparing sleep (code %d)\n", evt->data.err);
        } else {
            printk("APP: ignoring event %s while preparing sleep\n", app_event_type_name(type));
        }
        return;
    }

    switch (type) {
    case APP_EVT_BOOT:
        if (current_state == APP_STATE_BOOT) {
            change_state(APP_STATE_INIT_HW);
            on_enter(APP_STATE_INIT_HW);
        }
        break;

    case APP_EVT_BT_READY:
        if (current_state == APP_STATE_INIT_BT) {
            if (pending_clear_bonds) {
                printk("APP: performing pending clear bonds before advertising\n");
                int err = ble_app_clear_bonds();
                if (err == 0) {
                    input_combo_unpair_set_done(true);
                }
                pending_clear_bonds = false;
            } else {
                change_state(APP_STATE_ADVERTISING);
                on_enter(APP_STATE_ADVERTISING);
            }
        }
        break;

    case APP_EVT_BLE_CONNECTED:
        printk("APP: BLE connected\n");
        cancel_advertising_restart();
        if (current_state == APP_STATE_ADVERTISING) {
            change_state(APP_STATE_ACTIVE);
            on_enter(APP_STATE_ACTIVE);
        }
        break;

    case APP_EVT_BLE_DISCONNECTED:
        printk("APP: BLE disconnected (reason %d)\n", evt->data.ble.reason);
        if (ble_app_active_connections() == 0) {
            if (current_state == APP_STATE_LOW_POWER_CONNECTED) {
                change_state(APP_STATE_PREPARE_SLEEP);
                on_enter(APP_STATE_PREPARE_SLEEP);
                break;
            }

            if (evt->data.ble.reason == 0x13 || evt->data.ble.reason == 0x24) {
                schedule_advertising_restart(evt->data.ble.reason);
                break;
            }
            change_state(APP_STATE_ADVERTISING);
            on_enter(APP_STATE_ADVERTISING);
        }
        /* If there are still active connections, stay in the current connected state. */
        break;

    case APP_EVT_ADVERTISING_RESTART:
        if (current_state == APP_STATE_ADVERTISING &&
            ble_app_active_connections() == 0) {
            on_enter(APP_STATE_ADVERTISING);
        } else {
            printk("APP: advertising restart skipped by state/connections\n");
        }
        break;

    case APP_EVT_BLE_SECURITY_CHANGED:
        printk("APP: Security changed\n");
        break;

    case APP_EVT_BLE_HID_READY:
        printk("APP: BLE HID ready\n");
        cancel_advertising_restart();
        reconnect_fail_count = 0;
        break;

    case APP_EVT_BLE_SECURITY_FAILED:
        printk("APP: BLE security failed err=%d\n", evt->data.err);
        if (evt->data.err == 9 || evt->data.err == 4 || evt->data.err == 2) {
            printk("APP: security failed; likely stale bond on host or device\n");
        }
        break;

    case APP_EVT_CLEAR_BONDS_REQUESTED:
        printk("APP: clear bonds requested\n");
        if (current_state == APP_STATE_INIT_BT) {
            pending_clear_bonds = true;
            printk("APP: clear bonds pending until BT ready\n");
        } else {
            int err = ble_app_clear_bonds();
            if (err == 0) {
                input_combo_unpair_set_done(true);
            }
        }
        break;

    case APP_EVT_KEY_PRESSED:
    case APP_EVT_KEY_RELEASED:
    case APP_EVT_ENCODER_CW:
    case APP_EVT_ENCODER_CCW:
    case APP_EVT_ENCODER_DELTA:
    case APP_EVT_ENCODER_BUTTON_PRESSED:
    case APP_EVT_ENCODER_BUTTON_RELEASED:
        app_handle_input_event_common(evt);
        break;

    case APP_EVT_INACTIVITY_TIMEOUT:
        printk("APP: Inactivity timeout\n");
        if (current_state == APP_STATE_ACTIVE &&
            ble_app_active_connections() > 0) {
            change_state(APP_STATE_LOW_POWER_CONNECTED);
            on_enter(APP_STATE_LOW_POWER_CONNECTED);
        } else if (current_state != APP_STATE_PREPARE_SLEEP &&
                   current_state != APP_STATE_SYSTEM_OFF) {
            change_state(APP_STATE_PREPARE_SLEEP);
            on_enter(APP_STATE_PREPARE_SLEEP);
        }
        break;

    case APP_EVT_BATTERY_TICK:
        /* Handled by battery module, no state change needed */
        break;

    case APP_EVT_PROFILE_CHANGED:
        printk("APP: Profile changed to %d\n", evt->data.u8.value);
        input_apply_profile_change(evt->data.u8.value);
        break;

    case APP_EVT_BACKLIGHT_CHANGED:
        printk("APP: Backlight changed\n");
        input_apply_backlight_command(evt->data.u8.value);
        break;

    case APP_EVT_SLEEP_TIMEOUT_CHANGED:
        printk("APP: Sleep timeout changed\n");
        input_apply_sleep_timeout(evt->data.u8.value);
        break;

    case APP_EVT_SEQUENCE_RUN_REQUESTED:
        printk("APP: Sequence run requested\n");
        input_execute_sequence(evt->data.u8.value);
        break;

    case APP_EVT_PREPARE_SLEEP:
        if (current_state != APP_STATE_PREPARE_SLEEP) {
            change_state(APP_STATE_PREPARE_SLEEP);
            on_enter(APP_STATE_PREPARE_SLEEP);
        }
        break;

    case APP_EVT_ENTER_SYSTEM_OFF:
        if (current_state != APP_STATE_PREPARE_SLEEP &&
            current_state != APP_STATE_SYSTEM_OFF) {
            change_state(APP_STATE_PREPARE_SLEEP);
            on_enter(APP_STATE_PREPARE_SLEEP);
        }
        break;

    case APP_EVT_ERROR:
        printk("APP: Error event (code %d)\n", evt->data.err);
        change_state(APP_STATE_ERROR);
        break;

    default:
        printk("APP: Unknown event %d\n", type);
        break;
    }
}

/**
 * @brief State Machine Thread
 */
K_THREAD_STACK_DEFINE(app_sm_stack, 4096);
static struct k_thread app_sm_thread;

static void app_sm_thread_fn(void *p1, void *p2, void *p3)
{
    (void)p1;
    (void)p2;
    (void)p3;

    printk("\n=== APP STATE MACHINE STARTED ===\n");

    /* Post boot event */
    app_post_simple(APP_EVT_BOOT);

    /* Main loop: wait for events and handle them */
    while (1) {
        struct app_event evt;
        int err = app_wait_event(&evt);
        if (err) {
            printk("APP: app_wait_event failed (err %d)\n", err);
            continue;
        }

        handle_event(&evt);
    }
}

void app_sm_init(void)
{
    printk("APP: State machine initialized\n");
}

void app_sm_start(void)
{
    printk("APP: Starting state machine thread\n");
    k_thread_create(&app_sm_thread, app_sm_stack, K_THREAD_STACK_SIZEOF(app_sm_stack),
                    app_sm_thread_fn, NULL, NULL, NULL,
                    K_PRIO_COOP(7), 0, K_NO_WAIT);
}
