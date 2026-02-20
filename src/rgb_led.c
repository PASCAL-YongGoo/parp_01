/**
 * @file rgb_led.c
 * @brief SK6812 RGB LED — bit-bang with DWT cycle counter
 *
 * CPU core = 550MHz (SYSCLK/d1cpre), DWT CYCCNT counts at core clock.
 * SK6812 800kHz: T0H=165, T0L=495, T1H=330, T1L=330 cycles @550MHz.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "rgb_led.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(rgb_led, LOG_LEVEL_INF);

#define RGB_LED_NODE DT_ALIAS(rgb_led)

#if !DT_NODE_EXISTS(RGB_LED_NODE)
#error "rgb-led alias not found in device tree"
#endif

static const struct gpio_dt_spec rgb_led_pin = GPIO_DT_SPEC_GET(RGB_LED_NODE, gpios);

static uint8_t led_buffer[RGB_LED_COUNT * 3];
static uint8_t brightness_percent = 100;
static volatile bool initialized;

/* Unified LED state machine */
static volatile bool led_dirty;
static bool inventory_running;
static bool tag_blink_active;
static int64_t tag_blink_start;
static bool error_active;
static bool error_on;
static int64_t error_last;

#define TAG_BLINK_DURATION_MS  150
#define ERROR_BLINK_INTERVAL_MS 200
#define LED_BRIGHTNESS 64

/* Direct GPIO register access */
static volatile uint32_t *gpio_bsrr;
static uint32_t gpio_set_mask;
static uint32_t gpio_reset_mask;

/* DWT cycle counter (ARM Cortex-M debug hardware) */
#define DWT_CTRL_REG    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT_REG  (*(volatile uint32_t *)0xE0001004)
#define DEMCR_REG       (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL_CYCCNTENA  BIT(0)
#define DEMCR_TRCENA        BIT(24)

/*
 * SK6812 timing @ 550 MHz CPU core (1 cycle = 1.818 ns):
 *   T0H: 300ns = 165 cycles    T0L: 900ns = 495 cycles
 *   T1H: 600ns = 330 cycles    T1L: 600ns = 330 cycles
 *
 * Previous bug: calculated at 275MHz (AHB), but DWT runs at 550MHz (core).
 */
#define T0H_CYCLES  165
#define T0L_CYCLES  495
#define T1H_CYCLES  330
#define T1L_CYCLES  330

static inline void dwt_delay(uint32_t cycles)
{
	uint32_t start = DWT_CYCCNT_REG;
	while ((DWT_CYCCNT_REG - start) < cycles) {
	}
}

static void dwt_cycle_counter_init(void)
{
	DEMCR_REG |= DEMCR_TRCENA;
	DWT_CYCCNT_REG = 0;
	DWT_CTRL_REG |= DWT_CTRL_CYCCNTENA;
}

static inline void send_bit(bool bit)
{
	if (bit) {
		*gpio_bsrr = gpio_set_mask;
		dwt_delay(T1H_CYCLES);
		*gpio_bsrr = gpio_reset_mask;
		dwt_delay(T1L_CYCLES);
	} else {
		*gpio_bsrr = gpio_set_mask;
		dwt_delay(T0H_CYCLES);
		*gpio_bsrr = gpio_reset_mask;
		dwt_delay(T0L_CYCLES);
	}
}

static inline void send_byte(uint8_t byte)
{
	for (int i = 7; i >= 0; i--) {
		send_bit((byte >> i) & 0x01);
	}
}

static uint8_t apply_brightness(uint8_t value)
{
	return (uint8_t)((uint16_t)value * brightness_percent / 100);
}

/* Set all 7 LEDs to the base color for current state */
static void apply_base_color(void)
{
	if (inventory_running) {
		rgb_led_set_all(0, 0, LED_BRIGHTNESS);
	} else {
		rgb_led_set_all(LED_BRIGHTNESS, 0, 0);
	}
}

int rgb_led_init(void)
{
	if (!gpio_is_ready_dt(&rgb_led_pin)) {
		LOG_ERR("RGB LED GPIO not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&rgb_led_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure RGB LED GPIO: %d", ret);
		return ret;
	}

	dwt_cycle_counter_init();

	uint8_t pin = rgb_led_pin.pin;
	gpio_set_mask = BIT(pin);
	gpio_reset_mask = BIT(pin + 16);

	/*
	 * BSRR = GPIO base + 0x18.
	 * Access base from driver config: common + uint32_t *base.
	 */
	struct gpio_stm32_config_min {
		struct gpio_driver_config common;
		uint32_t *base;
	};
	const struct gpio_stm32_config_min *gpio_cfg =
		(const struct gpio_stm32_config_min *)rgb_led_pin.port->config;
	uintptr_t gpio_base = (uintptr_t)gpio_cfg->base;
	gpio_bsrr = (volatile uint32_t *)(gpio_base + 0x18);

	LOG_INF("GPIO base=0x%08lx, BSRR=0x%08lx, pin=%u",
		(unsigned long)gpio_base, (unsigned long)gpio_bsrr, pin);

	memset(led_buffer, 0, sizeof(led_buffer));
	initialized = true;

	/* Initial state: inventory OFF → RED */
	apply_base_color();
	rgb_led_update();

	LOG_INF("RGB LED initialized (%d LEDs, DWT @550MHz)", RGB_LED_COUNT);
	return 0;
}

void rgb_led_set_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	if (index >= RGB_LED_COUNT) {
		return;
	}
	uint8_t *pixel = &led_buffer[index * 3];
	pixel[0] = g;
	pixel[1] = r;
	pixel[2] = b;
}

void rgb_led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
	for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
		rgb_led_set_pixel(i, r, g, b);
	}
}

void rgb_led_clear(void)
{
	memset(led_buffer, 0, sizeof(led_buffer));
}

void rgb_led_update(void)
{
	if (!initialized) {
		return;
	}

	unsigned int key = irq_lock();
	for (size_t i = 0; i < sizeof(led_buffer); i++) {
		send_byte(apply_brightness(led_buffer[i]));
	}
	irq_unlock(key);

	k_busy_wait(100);
}

void rgb_led_set_brightness(uint8_t percent)
{
	if (percent > 100) {
		percent = 100;
	}
	brightness_percent = percent;
}

uint8_t rgb_led_get_brightness(void)
{
	return brightness_percent;
}

void rgb_led_test(void)
{
	if (!initialized) {
		return;
	}

	LOG_INF("RGB LED test starting...");

	const rgb_color_t colors[] = {
		RGB_COLOR_RED, RGB_COLOR_GREEN, RGB_COLOR_BLUE,
		RGB_COLOR_WHITE, RGB_COLOR_YELLOW, RGB_COLOR_CYAN,
		RGB_COLOR_MAGENTA, RGB_COLOR_OFF
	};

	for (size_t c = 0; c < ARRAY_SIZE(colors); c++) {
		rgb_led_set_all(colors[c].r, colors[c].g, colors[c].b);
		rgb_led_update();
		k_msleep(500);
	}

	apply_base_color();
	rgb_led_update();
	LOG_INF("RGB LED test complete");
}

void rgb_led_poll(void)
{
	if (!initialized) {
		return;
	}

	int64_t now = k_uptime_get();

	/* Error blink has highest priority */
	if (error_active) {
		if ((now - error_last) >= ERROR_BLINK_INTERVAL_MS) {
			error_last = now;
			error_on = !error_on;
			if (error_on) {
				rgb_led_set_all(LED_BRIGHTNESS, 0, 0);
			} else {
				rgb_led_clear();
			}
			led_dirty = true;
		}
	}

	/* Tag blink timeout → return to base color */
	if (tag_blink_active && (now - tag_blink_start) >= TAG_BLINK_DURATION_MS) {
		tag_blink_active = false;
		if (!error_active) {
			apply_base_color();
			led_dirty = true;
		}
	}

	if (led_dirty) {
		rgb_led_update();
		led_dirty = false;
	}
}

void rgb_led_set_inventory_status(bool running)
{
	inventory_running = running;
	if (!error_active && !tag_blink_active) {
		apply_base_color();
		led_dirty = true;
	}
}

void rgb_led_notify_tag_read(void)
{
	if (error_active) {
		return;
	}
	rgb_led_clear();
	tag_blink_active = true;
	tag_blink_start = k_uptime_get();
	led_dirty = true;
}

void rgb_led_set_error(bool active)
{
	error_active = active;
	if (!active) {
		apply_base_color();
		led_dirty = true;
	} else {
		error_last = k_uptime_get();
		error_on = true;
		rgb_led_set_all(LED_BRIGHTNESS, 0, 0);
		led_dirty = true;
	}
}

/* ========================================================================
 * Shell Commands
 * ======================================================================== */

static int cmd_rgb_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 5) {
		shell_print(sh, "Usage: rgb set <index> <r> <g> <b>");
		return -EINVAL;
	}

	int idx = atoi(argv[1]);
	if (idx < 0 || idx >= RGB_LED_COUNT) {
		shell_error(sh, "Invalid index (0-%d)", RGB_LED_COUNT - 1);
		return -EINVAL;
	}

	rgb_led_set_pixel((uint8_t)idx,
			  (uint8_t)atoi(argv[2]),
			  (uint8_t)atoi(argv[3]),
			  (uint8_t)atoi(argv[4]));
	rgb_led_update();
	shell_print(sh, "LED %d set", idx);
	return 0;
}

static int cmd_rgb_all(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_print(sh, "Usage: rgb all <r> <g> <b>");
		return -EINVAL;
	}

	rgb_led_set_all((uint8_t)atoi(argv[1]),
			(uint8_t)atoi(argv[2]),
			(uint8_t)atoi(argv[3]));
	rgb_led_update();
	shell_print(sh, "All LEDs set");
	return 0;
}

static int cmd_rgb_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	rgb_led_clear();
	rgb_led_update();
	shell_print(sh, "All LEDs off");
	return 0;
}

static int cmd_rgb_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!initialized) {
		shell_error(sh, "Not initialized");
		return -ENODEV;
	}
	shell_print(sh, "Running test...");
	rgb_led_test();
	shell_print(sh, "Done");
	return 0;
}

static int cmd_rgb_brightness(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Brightness: %u%%", brightness_percent);
		return 0;
	}

	int percent = atoi(argv[1]);
	if (percent < 0 || percent > 100) {
		shell_error(sh, "Must be 0-100");
		return -EINVAL;
	}

	rgb_led_set_brightness((uint8_t)percent);
	rgb_led_update();
	shell_print(sh, "Brightness: %u%%", brightness_percent);
	return 0;
}

static int cmd_rgb_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Initialized: %s", initialized ? "yes" : "no");
	shell_print(sh, "Inventory: %s", inventory_running ? "ON (BLUE)" : "OFF (RED)");
	shell_print(sh, "Error: %s", error_active ? "BLINKING" : "none");
	shell_print(sh, "Tag blink: %s", tag_blink_active ? "active" : "idle");
	shell_print(sh, "Brightness: %u%%", brightness_percent);
	shell_print(sh, "DWT timing: T0H=%d T0L=%d T1H=%d T1L=%d @550MHz",
		    T0H_CYCLES, T0L_CYCLES, T1H_CYCLES, T1L_CYCLES);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_rgb,
	SHELL_CMD(set, NULL, "Set LED: <idx> <r> <g> <b>", cmd_rgb_set),
	SHELL_CMD(all, NULL, "Set all: <r> <g> <b>", cmd_rgb_all),
	SHELL_CMD(clear, NULL, "Turn off all", cmd_rgb_clear),
	SHELL_CMD(test, NULL, "Color cycle test", cmd_rgb_test),
	SHELL_CMD(brightness, NULL, "Get/set brightness", cmd_rgb_brightness),
	SHELL_CMD(status, NULL, "Show state", cmd_rgb_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(rgb, &sub_rgb, "RGB LED control", NULL);
