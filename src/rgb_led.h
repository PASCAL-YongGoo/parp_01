/**
 * @file rgb_led.h
 * @brief SK6812 RGB LED Control via Bit-banging
 *
 * Controls 7 SK6812 RGB LEDs on PG2 as a unified status indicator.
 * All 7 LEDs display the same color/state simultaneously.
 *
 * States:
 *   Inventory OFF  → solid RED
 *   Inventory ON   → solid BLUE
 *   Tag read       → BLUE blink (100ms)
 *   Error          → RED blink (200ms toggle)
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

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} rgb_color_t;

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
 * @return 0 on success, negative errno on error
 */
int rgb_led_init(void);

/**
 * @brief Set single LED color
 */
void rgb_led_set_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set all LEDs to same color
 */
void rgb_led_set_all(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Clear all LEDs (turn off)
 */
void rgb_led_clear(void);

/**
 * @brief Transmit current buffer to LEDs
 */
void rgb_led_update(void);

/**
 * @brief Set global brightness (0-100%)
 */
void rgb_led_set_brightness(uint8_t percent);

/**
 * @brief Get current brightness
 */
uint8_t rgb_led_get_brightness(void);

/**
 * @brief Run color cycle test
 */
void rgb_led_test(void);

/**
 * @brief Poll LED state — call from main loop
 *
 * Handles tag blink timeout, error blink, and batched updates.
 */
void rgb_led_poll(void);

/**
 * @brief Set inventory status (all 7 LEDs)
 *
 * @param running true → solid BLUE, false → solid RED
 */
void rgb_led_set_inventory_status(bool running);

/**
 * @brief Notify tag read — all LEDs blink BLUE for 100ms
 */
void rgb_led_notify_tag_read(void);

/**
 * @brief Set error state — all LEDs blink RED
 *
 * @param active true to start blinking, false to stop
 */
void rgb_led_set_error(bool active);

#ifdef __cplusplus
}
#endif

#endif /* RGB_LED_H_ */
