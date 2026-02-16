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

LOG_MODULE_REGISTER(switch_control, LOG_LEVEL_INF);

/* SW0 device tree reference */
#define SW0_NODE DT_ALIAS(sw0)

#if !DT_NODE_EXISTS(SW0_NODE)
#error "sw0 alias not found in device tree"
#endif

static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* State variables */
static bool inventory_running = true;  /* Start with inventory running */
static inventory_toggle_cb_t toggle_callback;
static struct gpio_callback sw0_cb_data;

/* Debounce work queue */
static struct k_work_delayable debounce_work;
static int64_t last_press_time;

/**
 * @brief Debounce handler - called after debounce delay
 */
static void debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Toggle inventory state */
	inventory_running = !inventory_running;

	LOG_INF("SW0: Inventory %s", inventory_running ? "STARTED" : "STOPPED");

	/* Call registered callback */
	if (toggle_callback) {
		toggle_callback(inventory_running);
	}
}

/**
 * @brief SW0 GPIO interrupt callback
 */
static void sw0_pressed(const struct device *dev,
			struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int64_t now = k_uptime_get();

	/* Debounce check - ignore if too soon after last press */
	if ((now - last_press_time) < SWITCH_DEBOUNCE_MS) {
		return;
	}
	last_press_time = now;

	/* Schedule debounced handler */
	k_work_schedule(&debounce_work, K_MSEC(SWITCH_DEBOUNCE_MS));
}

int switch_control_init(void)
{
	int ret;

	/* Check if GPIO is ready */
	if (!gpio_is_ready_dt(&sw0)) {
		LOG_ERR("SW0 GPIO device not ready");
		return -ENODEV;
	}

	/* Configure SW0 as input */
	ret = gpio_pin_configure_dt(&sw0, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure SW0 GPIO: %d", ret);
		return ret;
	}

	/* Initialize debounce work (before callback registration) */
	k_work_init_delayable(&debounce_work, debounce_handler);

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
