/**
 * @file beep_control.h
 * @brief Beeper Control with E310 Input Detection
 *
 * Provides beeper output control (PF8) with duplicate filtering,
 * and E310 beep signal input detection (PG0).
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef BEEP_CONTROL_H_
#define BEEP_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup beep_control Beeper Control
 * @{
 */

/* ========================================================================
 * Constants
 * ======================================================================== */

/** Default beep pulse width in milliseconds */
#define BEEP_DEFAULT_PULSE_MS     100

/** Default duplicate filter time in milliseconds */
#define BEEP_DEFAULT_FILTER_MS    1000

/** Minimum pulse width */
#define BEEP_MIN_PULSE_MS         10

/** Maximum pulse width */
#define BEEP_MAX_PULSE_MS         1000

/** Minimum filter time */
#define BEEP_MIN_FILTER_MS        100

/** Maximum filter time */
#define BEEP_MAX_FILTER_MS        10000

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize beep control module
 *
 * Configures PF8 as beeper output and PG0 as E310 beep input with interrupt.
 *
 * @return 0 on success, negative errno on error
 */
int beep_control_init(void);

/**
 * @brief Trigger beeper output once
 *
 * Outputs a single beep pulse on PF8. Applies duplicate filtering
 * if called within the filter time window.
 */
void beep_control_trigger(void);

/**
 * @brief Force trigger beeper (bypass filter)
 *
 * Outputs a beep pulse without duplicate filtering.
 */
void beep_control_trigger_force(void);

/**
 * @brief Set beep pulse width
 *
 * @param ms Pulse width in milliseconds (10-1000)
 */
void beep_control_set_pulse_ms(uint16_t ms);

/**
 * @brief Get current beep pulse width
 *
 * @return Pulse width in milliseconds
 */
uint16_t beep_control_get_pulse_ms(void);

/**
 * @brief Set duplicate filter time
 *
 * Beep requests within this time window from the last beep are ignored.
 *
 * @param ms Filter time in milliseconds (100-10000)
 */
void beep_control_set_filter_ms(uint16_t ms);

/**
 * @brief Get current duplicate filter time
 *
 * @return Filter time in milliseconds
 */
uint16_t beep_control_get_filter_ms(void);

/**
 * @brief Enable/disable E310 beep input (PG0)
 *
 * When enabled, rising edge on PG0 will trigger the beeper.
 *
 * @param enable true to enable, false to disable
 */
void beep_control_enable_e310_input(bool enable);

/**
 * @brief Check if E310 input is enabled
 *
 * @return true if enabled, false otherwise
 */
bool beep_control_is_e310_input_enabled(void);

/**
 * @brief Get beep trigger count
 *
 * @return Number of beeps triggered since init
 */
uint32_t beep_control_get_count(void);

/**
 * @brief Reset beep trigger count
 */
void beep_control_reset_count(void);

/** @} */ /* End of beep_control group */

#ifdef __cplusplus
}
#endif

#endif /* BEEP_CONTROL_H_ */
