/** @file
 *  @brief HoG Service sample
 */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include "app_events.h"

#ifdef __cplusplus
extern "C" {
#endif

void hog_init(void);

void hog_button_loop(void);
void input_scan_init(void);

/* Input state query helpers for combo unpair */
uint8_t input_keyboard_pressed_count(void);
bool input_encoder_button_is_pressed(void);
bool input_combo_unpair_condition(void);
bool input_combo_unpair_already_done(void);
void input_combo_unpair_set_done(bool done);

/* Input scanning control for sleep handling */
void input_scan_set_enabled(bool enabled);
void input_enter_low_power_connected(void);
void input_exit_low_power_connected(void);

/* Execute encoder HID/local actions from state machine */
void input_apply_profile_change(uint8_t profile_index);
void input_apply_backlight_command(uint8_t value);
void input_apply_sleep_timeout(uint8_t minutes);
void input_execute_sequence(uint8_t sequence_index);
void input_handle_key_pressed(uint8_t row, uint8_t col);
void input_execute_encoder_action(bool clockwise);
void input_execute_encoder_delta(int delta);
void input_handle_encoder_button_event(enum app_event_type type);
void hid_send_current_input_state(void);

/* Bluetooth bonding helper */
int ble_app_clear_bonds(void);

/* Runtime interrupt control for system off */
void input_disable_runtime_interrupts(void);

/* Inactivity timer control */
void inactivity_timer_init(void);
void inactivity_timer_reset(void);
void inactivity_timer_reset_system_off_timeout(void);
void inactivity_timer_stop(void);

/* Power management entry points */
void power_prepare_system_off(void);
void power_enter_system_off(void);

#ifdef __cplusplus
}
#endif
