/*
 * PARP-01 Application
 * - Console output via UART1 (PB14-TX, PB15-RX with TX/RX swapped)
 * - LED blink on PE6 (TEST_LED)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(parp01, LOG_LEVEL_INF);

/* LED device tree reference */
#define TEST_LED_NODE DT_ALIAS(testled)

#if !DT_NODE_EXISTS(TEST_LED_NODE)
#error "testled alias not found in device tree. Please define it in board DTS."
#endif

static const struct gpio_dt_spec test_led = GPIO_DT_SPEC_GET(TEST_LED_NODE, gpios);

int main(void)
{
	int ret;
	bool led_state = false;
	uint32_t count = 0;

	printk("\n\n");
	printk("========================================\n");
	printk("  PARP-01 Custom Board Application\n");
	printk("========================================\n");
	printk("SYSCLK: 275 MHz (max safe for STM32H723)\n");
	printk("Console: UART1 (PB14-TX, PB15-RX, swapped)\n");
	printk("LED: PE6 (TEST_LED)\n");
	printk("========================================\n\n");

	/* Configure LED GPIO */
	if (!gpio_is_ready_dt(&test_led)) {
		LOG_ERR("LED GPIO device not ready");
		k_sleep(K_FOREVER);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&test_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED GPIO: %d", ret);
		k_sleep(K_FOREVER);
		return ret;
	}

	LOG_INF("System initialized successfully");
	LOG_INF("Starting LED blink loop...");

	/* Main loop: blink LED and print status */
	while (1) {
		/* Toggle LED */
		led_state = !led_state;
		ret = gpio_pin_set_dt(&test_led, led_state);
		if (ret < 0) {
			LOG_WRN("Failed to set LED state: %d", ret);
		}

		/* Print status */
		printk("[%05u] LED %s\n", count++, led_state ? "ON " : "OFF");

		/* Sleep 500ms */
		k_msleep(500);
	}

	return 0;
}
