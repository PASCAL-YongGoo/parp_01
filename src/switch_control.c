/**
 * @file switch_control.c
 * @brief Switch Control Implementation
 *
 * Handles SW0 button press with debouncing to toggle inventory mode.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "switch_control.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(switch_control, LOG_LEVEL_INF);

/* Diagnostic counters — all updated from ISR, read from shell */
static atomic_t diag_isr_count;        /* EXTI interrupt fired */
static atomic_t diag_debounce_reject;  /* Rejected by 300ms lockout */
static atomic_t diag_work_scheduled;   /* k_work_schedule succeeded */
static atomic_t diag_toggle_count;     /* toggle_handler actually ran */

/* SW0 device tree reference */
#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_EXISTS(SW0_NODE)
#error "sw0 alias not found in device tree"
#endif

static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* PD13 - Switch power supply (must output LOW for SW0/SW1 to work) */
#define SW_PWR_NODE DT_ALIAS(sw_pwr)

#if !DT_NODE_EXISTS(SW_PWR_NODE)
#error "sw-pwr alias not found in device tree"
#endif

static const struct gpio_dt_spec sw_pwr = GPIO_DT_SPEC_GET(SW_PWR_NODE, gpios);

/* State variables */
static bool inventory_running = false;
static inventory_toggle_cb_t toggle_callback;
static struct gpio_callback sw0_cb_data;

static struct k_work_delayable debounce_work;
static int64_t last_press_time;

static void toggle_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	atomic_inc(&diag_toggle_count);

	inventory_running = !inventory_running;
	LOG_INF("SW0: Inventory %s (isr=%d deb=%d sched=%d tog=%d)",
		inventory_running ? "STARTED" : "STOPPED",
		(int)atomic_get(&diag_isr_count),
		(int)atomic_get(&diag_debounce_reject),
		(int)atomic_get(&diag_work_scheduled),
		(int)atomic_get(&diag_toggle_count));

	if (toggle_callback) {
		toggle_callback(inventory_running);
	}
}

static void sw0_pressed(const struct device *dev,
			struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	atomic_inc(&diag_isr_count);

	int64_t now = k_uptime_get();

	if ((now - last_press_time) < SWITCH_DEBOUNCE_MS) {
		atomic_inc(&diag_debounce_reject);
		return;
	}
	last_press_time = now;

	int ret = k_work_schedule(&debounce_work, K_NO_WAIT);
	if (ret >= 0) {
		atomic_inc(&diag_work_scheduled);
	}
}

int switch_control_init(void)
{
	int ret;

	/* PD13 must output LOW before SW0/SW1 can detect falling edges */
	if (!gpio_is_ready_dt(&sw_pwr)) {
		LOG_ERR("SW_PWR (PD13) GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&sw_pwr, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure SW_PWR (PD13): %d", ret);
		return ret;
	}
	LOG_INF("SW_PWR (PD13) configured: output LOW");

	if (!gpio_is_ready_dt(&sw0)) {
		LOG_ERR("SW0 GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure SW0 GPIO: %d", ret);
		return ret;
	}

	/* Initialize debounce work (before callback registration) */
	k_work_init_delayable(&debounce_work, toggle_handler);

	/* Register callback BEFORE enabling interrupt to prevent
	 * race condition where interrupt fires before handler is set */
	gpio_init_callback(&sw0_cb_data, sw0_pressed, BIT(sw0.pin));
	ret = gpio_add_callback(sw0.port, &sw0_cb_data);
	if (ret < 0) {
		LOG_ERR("Failed to add SW0 callback: %d", ret);
		return ret;
	}

	/* Now enable interrupt (callback is already registered) */
	ret = gpio_pin_interrupt_configure_dt(&sw0, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure SW0 interrupt: %d", ret);
		/* Rollback: remove callback on failure */
		gpio_remove_callback(sw0.port, &sw0_cb_data);
		return ret;
	}

	LOG_INF("Switch control initialized (SW0 on PD10)");
	LOG_INF("Press SW0 to toggle inventory On/Off");

	return 0;
}

void switch_control_set_inventory_callback(inventory_toggle_cb_t cb)
{
	toggle_callback = cb;
}

bool switch_control_is_inventory_running(void)
{
	return inventory_running;
}

void switch_control_set_inventory_state(bool running)
{
	inventory_running = running;
}

/* ========================================================================
 * Shell Commands
 * ======================================================================== */

static int cmd_sw0_diag(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int pin_val = gpio_pin_get_dt(&sw0);

	shell_print(sh, "=== SW0 Diagnostics ===");
	shell_print(sh, "Pin state (raw): %d  (%s)",
		    pin_val, pin_val ? "PRESSED" : "released");
	shell_print(sh, "inventory_running: %s",
		    inventory_running ? "true" : "false");
	shell_print(sh, "--- Counters ---");
	shell_print(sh, "ISR fired:        %d", (int)atomic_get(&diag_isr_count));
	shell_print(sh, "Debounce reject:  %d", (int)atomic_get(&diag_debounce_reject));
	shell_print(sh, "Work scheduled:   %d", (int)atomic_get(&diag_work_scheduled));
	shell_print(sh, "Toggle executed:  %d", (int)atomic_get(&diag_toggle_count));
	return 0;
}

static int cmd_sw0_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	atomic_set(&diag_isr_count, 0);
	atomic_set(&diag_debounce_reject, 0);
	atomic_set(&diag_work_scheduled, 0);
	atomic_set(&diag_toggle_count, 0);
	shell_print(sh, "SW0 counters reset");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sw0,
	SHELL_CMD(diag, NULL, "Show SW0 diagnostics", cmd_sw0_diag),
	SHELL_CMD(reset, NULL, "Reset SW0 counters", cmd_sw0_reset),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sw0, &sub_sw0, "SW0 button diagnostics", NULL);
