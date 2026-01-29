/**
 * @file beep_control.c
 * @brief Beeper Control Implementation
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "beep_control.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(beep_control, LOG_LEVEL_INF);

/* Device tree references */
#define BEEP_OUT_NODE DT_ALIAS(beep_out)
#define E310_BEEP_NODE DT_ALIAS(e310_beep)

#if !DT_NODE_EXISTS(BEEP_OUT_NODE)
#error "beep-out alias not found in device tree"
#endif

#if !DT_NODE_EXISTS(E310_BEEP_NODE)
#error "e310-beep alias not found in device tree"
#endif

static const struct gpio_dt_spec beep_out = GPIO_DT_SPEC_GET(BEEP_OUT_NODE, gpios);
static const struct gpio_dt_spec e310_beep = GPIO_DT_SPEC_GET(E310_BEEP_NODE, gpios);

/* State variables */
static uint16_t beep_pulse_ms = BEEP_DEFAULT_PULSE_MS;
static uint16_t beep_filter_ms = BEEP_DEFAULT_FILTER_MS;
static bool e310_input_enabled = true;
static uint32_t beep_count;
static int64_t last_beep_time;
/* Initialization flag - volatile for ISR/main thread synchronization */
static volatile bool initialized;

/* GPIO callback for E310 input */
static struct gpio_callback e310_beep_cb_data;

/* Work queue for beep pulse */
static struct k_work_delayable beep_off_work;
static struct k_work beep_trigger_work;

/**
 * @brief Turn off beeper (work handler)
 */
static void beep_off_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	gpio_pin_set_dt(&beep_out, 0);
}

/**
 * @brief Execute beep pulse (work handler for ISR context)
 */
static void beep_trigger_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int64_t now = k_uptime_get();

	/* Apply duplicate filter */
	if ((now - last_beep_time) < beep_filter_ms) {
		LOG_DBG("Beep filtered (within %u ms window)", beep_filter_ms);
		return;
	}

	last_beep_time = now;
	beep_count++;

	/* Turn on beeper */
	gpio_pin_set_dt(&beep_out, 1);

	/* Schedule turn off */
	k_work_schedule(&beep_off_work, K_MSEC(beep_pulse_ms));

	LOG_DBG("Beep triggered (count=%u, pulse=%u ms)", beep_count, beep_pulse_ms);
}

/**
 * @brief E310 beep input interrupt callback (PG0 rising edge)
 */
static void e310_beep_isr(const struct device *dev,
			  struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (!e310_input_enabled) {
		return;
	}

	/* Schedule beep trigger from work queue (not ISR context) */
	k_work_submit(&beep_trigger_work);
}

int beep_control_init(void)
{
	int ret;

	/* Check beep output GPIO */
	if (!gpio_is_ready_dt(&beep_out)) {
		LOG_ERR("Beep output GPIO device not ready");
		return -ENODEV;
	}

	/* Configure beep output as LOW initially */
	ret = gpio_pin_configure_dt(&beep_out, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure beep output GPIO: %d", ret);
		return ret;
	}

	/* Check E310 beep input GPIO */
	if (!gpio_is_ready_dt(&e310_beep)) {
		LOG_ERR("E310 beep input GPIO device not ready");
		return -ENODEV;
	}

	/* Configure E310 beep input */
	ret = gpio_pin_configure_dt(&e310_beep, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure E310 beep input GPIO: %d", ret);
		return ret;
	}

	/*
	 * IMPORTANT: Register callback BEFORE enabling interrupt.
	 * This prevents spurious interrupts from being missed or
	 * causing undefined behavior if an edge occurs immediately.
	 */

	/* 1. Initialize and add GPIO callback first */
	gpio_init_callback(&e310_beep_cb_data, e310_beep_isr, BIT(e310_beep.pin));
	ret = gpio_add_callback(e310_beep.port, &e310_beep_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add E310 beep callback: %d", ret);
		return ret;
	}

	/* 2. Then configure interrupt on rising edge (E310 outputs HIGH when tag read) */
	ret = gpio_pin_interrupt_configure_dt(&e310_beep, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure E310 beep interrupt: %d", ret);
		gpio_remove_callback(e310_beep.port, &e310_beep_cb_data);
		return ret;
	}

	/* Initialize work items */
	k_work_init_delayable(&beep_off_work, beep_off_handler);
	k_work_init(&beep_trigger_work, beep_trigger_handler);

	initialized = true;

	LOG_INF("Beep control initialized");
	LOG_INF("  Output: PF8 (BEEP_FROM_MCU)");
	LOG_INF("  Input: PG0 (TY928_BEEP from E310)");
	LOG_INF("  Pulse: %u ms, Filter: %u ms", beep_pulse_ms, beep_filter_ms);

	return 0;
}

void beep_control_trigger(void)
{
	if (!initialized) {
		return;
	}

	/* Use work queue for thread safety */
	k_work_submit(&beep_trigger_work);
}

void beep_control_trigger_force(void)
{
	if (!initialized) {
		return;
	}

	/*
	 * Direct trigger bypassing filter.
	 * Use irq_lock to prevent race condition with ISR context
	 * accessing beep_count and last_beep_time.
	 */
	unsigned int key = irq_lock();
	beep_count++;
	last_beep_time = k_uptime_get();
	irq_unlock(key);

	gpio_pin_set_dt(&beep_out, 1);
	k_work_schedule(&beep_off_work, K_MSEC(beep_pulse_ms));

	LOG_DBG("Beep forced (count=%u)", beep_count);
}

void beep_control_set_pulse_ms(uint16_t ms)
{
	if (ms < BEEP_MIN_PULSE_MS) {
		ms = BEEP_MIN_PULSE_MS;
	} else if (ms > BEEP_MAX_PULSE_MS) {
		ms = BEEP_MAX_PULSE_MS;
	}
	beep_pulse_ms = ms;
	LOG_INF("Beep pulse set to %u ms", beep_pulse_ms);
}

uint16_t beep_control_get_pulse_ms(void)
{
	return beep_pulse_ms;
}

void beep_control_set_filter_ms(uint16_t ms)
{
	if (ms < BEEP_MIN_FILTER_MS) {
		ms = BEEP_MIN_FILTER_MS;
	} else if (ms > BEEP_MAX_FILTER_MS) {
		ms = BEEP_MAX_FILTER_MS;
	}
	beep_filter_ms = ms;
	LOG_INF("Beep filter set to %u ms", beep_filter_ms);
}

uint16_t beep_control_get_filter_ms(void)
{
	return beep_filter_ms;
}

void beep_control_enable_e310_input(bool enable)
{
	e310_input_enabled = enable;
	LOG_INF("E310 beep input %s", enable ? "enabled" : "disabled");
}

bool beep_control_is_e310_input_enabled(void)
{
	return e310_input_enabled;
}

uint32_t beep_control_get_count(void)
{
	return beep_count;
}

void beep_control_reset_count(void)
{
	beep_count = 0;
}

/* ========================================================================
 * Shell Commands
 * ======================================================================== */

static int cmd_beep_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!initialized) {
		shell_error(sh, "Beep control not initialized");
		return -ENODEV;
	}

	beep_control_trigger_force();
	shell_print(sh, "Beep test triggered (pulse=%u ms)", beep_pulse_ms);

	return 0;
}

static int cmd_beep_pulse(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Current pulse width: %u ms", beep_pulse_ms);
		shell_print(sh, "Usage: beep pulse <10-1000>");
		return 0;
	}

	int ms = atoi(argv[1]);
	if (ms < BEEP_MIN_PULSE_MS || ms > BEEP_MAX_PULSE_MS) {
		shell_error(sh, "Invalid pulse: %d (must be %d-%d)",
			    ms, BEEP_MIN_PULSE_MS, BEEP_MAX_PULSE_MS);
		return -EINVAL;
	}

	beep_control_set_pulse_ms((uint16_t)ms);
	shell_print(sh, "Pulse width set to %u ms", beep_pulse_ms);

	return 0;
}

static int cmd_beep_filter(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Current filter time: %u ms", beep_filter_ms);
		shell_print(sh, "Usage: beep filter <100-10000>");
		return 0;
	}

	int ms = atoi(argv[1]);
	if (ms < BEEP_MIN_FILTER_MS || ms > BEEP_MAX_FILTER_MS) {
		shell_error(sh, "Invalid filter: %d (must be %d-%d)",
			    ms, BEEP_MIN_FILTER_MS, BEEP_MAX_FILTER_MS);
		return -EINVAL;
	}

	beep_control_set_filter_ms((uint16_t)ms);
	shell_print(sh, "Filter time set to %u ms", beep_filter_ms);

	return 0;
}

static int cmd_beep_e310(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "E310 input: %s", e310_input_enabled ? "enabled" : "disabled");
		shell_print(sh, "Usage: beep e310 <on|off>");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		beep_control_enable_e310_input(true);
		shell_print(sh, "E310 beep input enabled");
	} else if (strcmp(argv[1], "off") == 0) {
		beep_control_enable_e310_input(false);
		shell_print(sh, "E310 beep input disabled");
	} else {
		shell_error(sh, "Invalid argument: %s (use on/off)", argv[1]);
		return -EINVAL;
	}

	return 0;
}

static int cmd_beep_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== Beep Control Status ===");
	shell_print(sh, "Initialized: %s", initialized ? "yes" : "no");
	shell_print(sh, "Pulse width: %u ms", beep_pulse_ms);
	shell_print(sh, "Filter time: %u ms", beep_filter_ms);
	shell_print(sh, "E310 input: %s", e310_input_enabled ? "enabled" : "disabled");
	shell_print(sh, "Beep count: %u", beep_count);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_beep,
	SHELL_CMD(test, NULL, "Trigger beep test", cmd_beep_test),
	SHELL_CMD(pulse, NULL, "Get/set pulse width (ms)", cmd_beep_pulse),
	SHELL_CMD(filter, NULL, "Get/set filter time (ms)", cmd_beep_filter),
	SHELL_CMD(e310, NULL, "Enable/disable E310 input", cmd_beep_e310),
	SHELL_CMD(status, NULL, "Show beep status", cmd_beep_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(beep, &sub_beep, "Beeper control commands", NULL);
