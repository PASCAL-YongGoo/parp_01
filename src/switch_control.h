/**
 * @file switch_control.h
 * @brief Switch Control for Inventory On/Off
 *
 * Provides hardware button control for RFID inventory mode.
 * SW0 (PD10) toggles inventory on/off with debouncing.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef SWITCH_CONTROL_H
#define SWITCH_CONTROL_H

#include <stdbool.h>

/** Debounce lockout in milliseconds - reject edges within this window */
#define SWITCH_DEBOUNCE_MS  300

/** Inventory toggle callback type */
typedef void (*inventory_toggle_cb_t)(bool start);

/**
 * @brief Initialize switch control
 *
 * Configures SW0 GPIO interrupt with debouncing.
 *
 * @return 0 on success, negative errno on error
 */
int switch_control_init(void);

/**
 * @brief Register inventory toggle callback
 *
 * @param cb Callback function to be called when inventory is toggled
 */
void switch_control_set_inventory_callback(inventory_toggle_cb_t cb);

/**
 * @brief Check if inventory is currently running
 *
 * @return true if inventory is running, false otherwise
 */
bool switch_control_is_inventory_running(void);

/**
 * @brief Set inventory running state
 *
 * Used to sync state when inventory is started/stopped by other means.
 *
 * @param running true to set as running, false to set as stopped
 */
void switch_control_set_inventory_state(bool running);

#endif /* SWITCH_CONTROL_H */
