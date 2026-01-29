/**
 * @file rgb_led.c
 * @brief SK6812 RGB LED Control Implementation
 *
 * Bit-banging implementation for SK6812 RGB LEDs.
 * STM32H723 at 275MHz, timing via NOP delays.
 *
 * SK6812 Protocol (800kHz):
 *   T0H: 0.3us (83 cycles @ 275MHz)
 *   T0L: 0.9us (248 cycles @ 275MHz)
 *   T1H: 0.6us (165 cycles @ 275MHz)
 *   T1L: 0.6us (165 cycles @ 275MHz)
 *   Reset: >80us
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

/* Device tree reference */
#define RGB_LED_NODE DT_ALIAS(rgb_led)

#if !DT_NODE_EXISTS(RGB_LED_NODE)
#error "rgb-led alias not found in device tree"
#endif

static const struct gpio_dt_spec rgb_led_pin = GPIO_DT_SPEC_GET(RGB_LED_NODE, gpios);

/* Color buffer (GRB order for SK6812) */
static uint8_t led_buffer[RGB_LED_COUNT * 3];

/* Brightness scaling (0-100%) */
static uint8_t brightness_percent = 100;

/* Initialization flag - volatile for ISR/main thread synchronization */
static volatile bool initialized;

/* Direct GPIO register access for timing-critical bit-banging */
static volatile uint32_t *gpio_bsrr;
static uint32_t gpio_set_mask;
static uint32_t gpio_reset_mask;

/**
 * @brief Inline delay using NOP instructions
 *
 * At 275MHz, 1 cycle = ~3.6ns
 * Approximate cycle counts for SK6812 timing.
 *
 * NOTE: Timing may vary due to Cortex-M7 pipeline and cache effects.
 * Verify with oscilloscope if LED behavior is unstable.
 * For more precise timing, consider using DWT cycle counter or
 * hardware timer/DMA-based implementation.
 */
static inline void delay_cycles(uint32_t cycles)
{
	/* Each NOP takes ~1 cycle on Cortex-M7 */
	/* Memory barrier prevents compiler reordering */
	while (cycles--) {
		__asm volatile ("nop" ::: "memory");
	}
}

/**
 * @brief Send a single bit to SK6812
 *
 * Timing-critical function - interrupts should be disabled.
 */
static inline void send_bit(bool bit)
{
	if (bit) {
		/* Send '1': T1H (0.6us) high, T1L (0.6us) low */
		*gpio_bsrr = gpio_set_mask;    /* Set HIGH */
		delay_cycles(140);             /* ~0.5us @ 275MHz */
		*gpio_bsrr = gpio_reset_mask;  /* Set LOW */
		delay_cycles(140);             /* ~0.5us @ 275MHz */
	} else {
		/* Send '0': T0H (0.3us) high, T0L (0.9us) low */
		*gpio_bsrr = gpio_set_mask;    /* Set HIGH */
		delay_cycles(60);              /* ~0.22us @ 275MHz */
		*gpio_bsrr = gpio_reset_mask;  /* Set LOW */
		delay_cycles(220);             /* ~0.8us @ 275MHz */
	}
}

/**
 * @brief Send a byte to SK6812 (MSB first)
 */
static inline void send_byte(uint8_t byte)
{
	for (int i = 7; i >= 0; i--) {
		send_bit((byte >> i) & 0x01);
	}
}

/**
 * @brief Apply brightness scaling to a color value
 */
static uint8_t apply_brightness(uint8_t value)
{
	return (uint8_t)((uint16_t)value * brightness_percent / 100);
}

int rgb_led_init(void)
{
	int ret;

	/* Check GPIO device */
	if (!gpio_is_ready_dt(&rgb_led_pin)) {
		LOG_ERR("RGB LED GPIO device not ready");
		return -ENODEV;
	}

	/* Configure as output, initially LOW */
	ret = gpio_pin_configure_dt(&rgb_led_pin, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure RGB LED GPIO: %d", ret);
		return ret;
	}

	/* Get direct register access for fast bit-banging */
	/* Get the pin number from pin spec */
	uint8_t pin = rgb_led_pin.pin;

	/* Set up masks for BSRR register */
	gpio_set_mask = BIT(pin);           /* Lower 16 bits: set */
	gpio_reset_mask = BIT(pin + 16);    /* Upper 16 bits: reset */

	/*
	 * Get BSRR register address from device tree.
	 * STM32H7 GPIO registers: base + 0x18 = BSRR
	 *
	 * Note: We derive the base address from the GPIO controller's
	 * register address in device tree, avoiding hardcoded addresses.
	 * GPIOG base: 0x58021800 (from STM32H7 reference manual)
	 */
	const struct device *gpio_dev = rgb_led_pin.port;
	uintptr_t gpio_base = (uintptr_t)DEVICE_MMIO_GET(gpio_dev);
	gpio_bsrr = (volatile uint32_t *)(gpio_base + 0x18);

	/* Clear buffer */
	memset(led_buffer, 0, sizeof(led_buffer));

	/* Send initial reset and clear LEDs */
	rgb_led_clear();
	rgb_led_update();

	initialized = true;

	LOG_INF("RGB LED initialized (%d LEDs on PG2)", RGB_LED_COUNT);

	return 0;
}

void rgb_led_set_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
	if (index >= RGB_LED_COUNT) {
		LOG_WRN("Invalid LED index: %u (max %u)", index, RGB_LED_COUNT - 1);
		return;
	}

	/* SK6812 uses GRB order */
	uint8_t *pixel = &led_buffer[index * 3];
	pixel[0] = g;  /* Green */
	pixel[1] = r;  /* Red */
	pixel[2] = b;  /* Blue */
}

void rgb_led_set_pixel_color(uint8_t index, rgb_color_t color)
{
	rgb_led_set_pixel(index, color.r, color.g, color.b);
}

void rgb_led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
	for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
		rgb_led_set_pixel(i, r, g, b);
	}
}

void rgb_led_set_all_color(rgb_color_t color)
{
	rgb_led_set_all(color.r, color.g, color.b);
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

	/*
	 * WARNING: Interrupt-critical section.
	 *
	 * SK6812 bit-banging requires precise timing that cannot tolerate
	 * interrupts. With 7 LEDs (21 bytes × 8 bits × ~1.25µs/bit ≈ 210µs),
	 * interrupts are disabled for approximately 210µs.
	 *
	 * Impact on other subsystems:
	 * - USB: May cause minor jitter, generally tolerable
	 * - UART RX: 115200 baud = 86µs/byte, may lose 2-3 bytes max
	 *
	 * For applications requiring more LEDs or stricter real-time
	 * requirements, consider migrating to SPI/DMA or PWM/DMA
	 * based implementation.
	 */
	unsigned int key = irq_lock();

	/* Send all bytes (GRB for each LED) */
	for (size_t i = 0; i < sizeof(led_buffer); i++) {
		uint8_t value = apply_brightness(led_buffer[i]);
		send_byte(value);
	}

	/* Re-enable interrupts */
	irq_unlock(key);

	/* Reset pulse: >80us low */
	k_busy_wait(100);
}

void rgb_led_set_brightness(uint8_t percent)
{
	if (percent > 100) {
		percent = 100;
	}
	brightness_percent = percent;
	LOG_INF("RGB LED brightness set to %u%%", brightness_percent);
}

uint8_t rgb_led_get_brightness(void)
{
	return brightness_percent;
}

void rgb_led_test(void)
{
	if (!initialized) {
		LOG_WRN("RGB LED not initialized");
		return;
	}

	LOG_INF("RGB LED test starting...");

	/* Cycle through colors */
	const rgb_color_t colors[] = {
		RGB_COLOR_RED,
		RGB_COLOR_GREEN,
		RGB_COLOR_BLUE,
		RGB_COLOR_WHITE,
		RGB_COLOR_YELLOW,
		RGB_COLOR_CYAN,
		RGB_COLOR_MAGENTA,
		RGB_COLOR_OFF
	};

	for (size_t c = 0; c < ARRAY_SIZE(colors); c++) {
		rgb_led_set_all_color(colors[c]);
		rgb_led_update();
		k_msleep(500);
	}

	LOG_INF("RGB LED test complete");
}

void rgb_led_set_pattern(uint8_t pattern)
{
	switch (pattern) {
	case 0:  /* Off */
		rgb_led_clear();
		break;
	case 1:  /* Ready (green) */
		rgb_led_set_all(0, 64, 0);
		break;
	case 2:  /* Active/busy (blue) */
		rgb_led_set_all(0, 0, 64);
		break;
	case 3:  /* Warning (yellow) */
		rgb_led_set_all(64, 64, 0);
		break;
	case 4:  /* Error (red) */
		rgb_led_set_all(64, 0, 0);
		break;
	case 5:  /* Success (green pulse - just green for now) */
		rgb_led_set_all(0, 128, 0);
		break;
	default:
		rgb_led_clear();
		break;
	}
	rgb_led_update();
}

/* ========================================================================
 * Shell Commands
 * ======================================================================== */

static int cmd_rgb_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 5) {
		shell_print(sh, "Usage: rgb set <index> <r> <g> <b>");
		shell_print(sh, "  index: 0-%d, r/g/b: 0-255", RGB_LED_COUNT - 1);
		return -EINVAL;
	}

	int idx = atoi(argv[1]);
	int r = atoi(argv[2]);
	int g = atoi(argv[3]);
	int b = atoi(argv[4]);

	if (idx < 0 || idx >= RGB_LED_COUNT) {
		shell_error(sh, "Invalid index: %d (must be 0-%d)", idx, RGB_LED_COUNT - 1);
		return -EINVAL;
	}

	rgb_led_set_pixel((uint8_t)idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
	rgb_led_update();

	shell_print(sh, "LED %d set to RGB(%d, %d, %d)", idx, r, g, b);
	return 0;
}

static int cmd_rgb_all(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 4) {
		shell_print(sh, "Usage: rgb all <r> <g> <b>");
		shell_print(sh, "  r/g/b: 0-255");
		return -EINVAL;
	}

	int r = atoi(argv[1]);
	int g = atoi(argv[2]);
	int b = atoi(argv[3]);

	rgb_led_set_all((uint8_t)r, (uint8_t)g, (uint8_t)b);
	rgb_led_update();

	shell_print(sh, "All LEDs set to RGB(%d, %d, %d)", r, g, b);
	return 0;
}

static int cmd_rgb_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rgb_led_clear();
	rgb_led_update();

	shell_print(sh, "All LEDs cleared");
	return 0;
}

static int cmd_rgb_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!initialized) {
		shell_error(sh, "RGB LED not initialized");
		return -ENODEV;
	}

	shell_print(sh, "Running RGB LED test...");
	rgb_led_test();
	shell_print(sh, "Test complete");

	return 0;
}

static int cmd_rgb_brightness(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Current brightness: %u%%", brightness_percent);
		shell_print(sh, "Usage: rgb brightness <0-100>");
		return 0;
	}

	int percent = atoi(argv[1]);
	if (percent < 0 || percent > 100) {
		shell_error(sh, "Invalid brightness: %d (must be 0-100)", percent);
		return -EINVAL;
	}

	rgb_led_set_brightness((uint8_t)percent);
	rgb_led_update();  /* Apply new brightness to current colors */

	shell_print(sh, "Brightness set to %u%%", brightness_percent);
	return 0;
}

static int cmd_rgb_pattern(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Usage: rgb pattern <0-5>");
		shell_print(sh, "  0: Off");
		shell_print(sh, "  1: Ready (green)");
		shell_print(sh, "  2: Active (blue)");
		shell_print(sh, "  3: Warning (yellow)");
		shell_print(sh, "  4: Error (red)");
		shell_print(sh, "  5: Success (green)");
		return -EINVAL;
	}

	int pattern = atoi(argv[1]);
	if (pattern < 0 || pattern > 5) {
		shell_error(sh, "Invalid pattern: %d (must be 0-5)", pattern);
		return -EINVAL;
	}

	rgb_led_set_pattern((uint8_t)pattern);
	shell_print(sh, "Pattern %d applied", pattern);

	return 0;
}

static int cmd_rgb_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== RGB LED Status ===");
	shell_print(sh, "Initialized: %s", initialized ? "yes" : "no");
	shell_print(sh, "LED count: %d", RGB_LED_COUNT);
	shell_print(sh, "Brightness: %u%%", brightness_percent);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_rgb,
	SHELL_CMD(set, NULL, "Set single LED: <idx> <r> <g> <b>", cmd_rgb_set),
	SHELL_CMD(all, NULL, "Set all LEDs: <r> <g> <b>", cmd_rgb_all),
	SHELL_CMD(clear, NULL, "Turn off all LEDs", cmd_rgb_clear),
	SHELL_CMD(test, NULL, "Run color test", cmd_rgb_test),
	SHELL_CMD(brightness, NULL, "Get/set brightness (0-100)", cmd_rgb_brightness),
	SHELL_CMD(pattern, NULL, "Set status pattern (0-5)", cmd_rgb_pattern),
	SHELL_CMD(status, NULL, "Show RGB LED status", cmd_rgb_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(rgb, &sub_rgb, "RGB LED control commands", NULL);
