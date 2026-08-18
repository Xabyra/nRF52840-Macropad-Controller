/** @file
 *  @brief Application State Machine
 *  
 *  Central state machine that makes strategic decisions about
 *  BLE advertising, sleep, active state transitions, etc.
 *  
 *  State transitions are driven by events posted to app_events queue.
 */

#ifndef APP_SM_H
#define APP_SM_H

#include <stdbool.h>

/**
 * @brief Initialize the state machine (but don't start it)
 * 
 * Should be called before app_sm_start()
 */
void app_sm_init(void);

/**
 * @brief Start the state machine
 * 
 * Creates and starts the state machine thread.
 * This will post APP_EVT_BOOT internally.
 */
void app_sm_start(void);

/**
 * @brief Get current state as string (for debug logging)
 * 
 * @return Pointer to static string with state name
 */
const char *app_sm_get_state_name(void);

#endif /* APP_SM_H */
