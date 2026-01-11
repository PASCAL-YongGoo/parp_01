/*
 * PARP-01 Application
 * - Console output via UART1 (PB14-TX, PB15-RX with TX/RX swapped)
 * - LED blink on PE6 (TEST_LED)
 * - E310 RFID Protocol Library Testing
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>

#include "usb_device.h"
#include "usb_hid.h"
#include "uart_router.h"

LOG_MODULE_REGISTER(parp01, LOG_LEVEL_INF);

/* E310 Protocol Library Test */
extern void e310_run_tests(void);

static struct usbd_context *usb_ctx;
static uart_router_t uart_router;

static void usb_msg_cb(struct usbd_context *const ctx,
		       const struct usbd_msg *const msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable USB device");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable USB device");
			}
		}
	}
}

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

	ret = usb_hid_init();
	if (ret < 0) {
		LOG_ERR("Failed to init USB HID: %d", ret);
		return ret;
	}

	usb_ctx = usb_device_init(usb_msg_cb);
	if (!usb_ctx) {
		LOG_ERR("Failed to initialize USB device stack");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(usb_ctx)) {
		ret = usbd_enable(usb_ctx);
		if (ret) {
			LOG_ERR("Failed to enable USB device: %d", ret);
			return ret;
		}
	}

	ret = uart_router_init(&uart_router);
	if (ret < 0) {
		LOG_ERR("Failed to init UART router: %d", ret);
		return ret;
	}

	ret = uart_router_start(&uart_router);
	if (ret < 0) {
		LOG_ERR("Failed to start UART router: %d", ret);
		return ret;
	}

	uart_router_set_mode(&uart_router, ROUTER_MODE_INVENTORY);

	/* Run E310 protocol library tests */
	printk("\n");
	e310_run_tests();
	printk("\n");

	LOG_INF("Starting LED blink loop...");

	/* Main loop: blink LED and print status */
	while (1) {
		uart_router_process(&uart_router);

		if (k_uptime_get() - (int64_t)count * 500 >= 500) {
			/* Toggle LED */
			led_state = !led_state;
			ret = gpio_pin_set_dt(&test_led, led_state);
			if (ret < 0) {
				LOG_WRN("Failed to set LED state: %d", ret);
			}

			/* Print status */
			printk("[%05u] LED %s\n", count++, led_state ? "ON " : "OFF");
		}

		k_msleep(10);
	}

	return 0;
}
