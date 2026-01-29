/**
 * @file rgb_led.h
 * @brief SK6812 RGB LED Control via Bit-banging
 *
 * Provides control for 7 SK6812 RGB LEDs connected to PG2.
 * Uses bit-banging protocol at 800kHz (GRB color order).
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef RGB_LED_H_
#define RGB_LED_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup rgb_led RGB LED Control
 * @{
 */

/* ========================================================================
 * Constants
 * ======================================================================== */

/** Number of RGB LEDs in the chain */
#define RGB_LED_COUNT         7

/** Maximum brightness value */
#define RGB_LED_MAX_BRIGHTNESS 255

/* ========================================================================
 * Predefined Colors
 * ======================================================================== */

/** Color structure */
typedef struct {
	uint8_t r;  /**< Red component (0-255) */
	uint8_t g;  /**< Green component (0-255) */
	uint8_t b;  /**< Blue component (0-255) */
} rgb_color_t;

/** Predefined colors */
#define RGB_COLOR_OFF     ((rgb_color_t){0, 0, 0})
#define RGB_COLOR_RED     ((rgb_color_t){255, 0, 0})
#define RGB_COLOR_GREEN   ((rgb_color_t){0, 255, 0})
#define RGB_COLOR_BLUE    ((rgb_color_t){0, 0, 255})
#define RGB_COLOR_WHITE   ((rgb_color_t){255, 255, 255})
#define RGB_COLOR_YELLOW  ((rgb_color_t){255, 255, 0})
#define RGB_COLOR_CYAN    ((rgb_color_t){0, 255, 255})
#define RGB_COLOR_MAGENTA ((rgb_color_t){255, 0, 255})
#define RGB_COLOR_ORANGE  ((rgb_color_t){255, 128, 0})

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize RGB LED module
 *
 * Configures PG2 as output for SK6812 data signal.
 *
 * @return 0 on success, negative errno on error
 */
int rgb_led_init(void);

/**
 * @brief Set single LED color
 *
 * @param index LED index (0 to RGB_LED_COUNT-1)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void rgb_led_set_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set single LED color using color structure
 *
 * @param index LED index (0 to RGB_LED_COUNT-1)
 * @param color Color structure
 */
void rgb_led_set_pixel_color(uint8_t index, rgb_color_t color);

/**
 * @brief Set all LEDs to same color
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void rgb_led_set_all(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set all LEDs using color structure
 *
 * @param color Color structure
 */
void rgb_led_set_all_color(rgb_color_t color);

/**
 * @brief Clear all LEDs (turn off)
 */
void rgb_led_clear(void);

/**
 * @brief Update LEDs with current buffer data
 *
 * Transmits the current color buffer to the LED chain.
 * Must be called after set_pixel/set_all to apply changes.
 */
void rgb_led_update(void);

/**
 * @brief Set global brightness scaling
 *
 * All color values are scaled by this percentage.
 *
 * @param percent Brightness percentage (0-100)
 */
void rgb_led_set_brightness(uint8_t percent);

/**
 * @brief Get current brightness setting
 *
 * @return Current brightness percentage (0-100)
 */
uint8_t rgb_led_get_brightness(void);

/**
 * @brief Run color test pattern
 *
 * Cycles through basic colors on all LEDs.
 */
void rgb_led_test(void);

/**
 * @brief Set status indicator pattern
 *
 * Predefined patterns for system status indication.
 *
 * @param pattern Pattern index:
 *   0 = Off
 *   1 = Ready (green)
 *   2 = Active/busy (blue)
 *   3 = Warning (yellow)
 *   4 = Error (red)
 *   5 = Success (green pulse)
 */
void rgb_led_set_pattern(uint8_t pattern);

/** @} */ /* End of rgb_led group */

#ifdef __cplusplus
}
#endif

#endif /* RGB_LED_H_ */
