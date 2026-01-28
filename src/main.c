/*
 * PARP-01 Application
 * - Console output via USB CDC ACM
 * - LED blink on PE6 (TEST_LED)
 * - E310 RFID Protocol Library Testing
 * - SW0 button for inventory control
 * - Shell login security
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include "usb_device.h"
#include "usb_hid.h"
#include "uart_router.h"
#include "switch_control.h"
#include "shell_login.h"
#include "password_storage.h"

LOG_MODULE_REGISTER(parp01, LOG_LEVEL_INF);

/* E310 Protocol Library Test */
extern void e310_run_tests(void);

static struct usbd_context *usb_ctx;
static uart_router_t uart_router;

/* USB host connection wait timeout (ms) */
#define USB_HOST_WAIT_TIMEOUT_MS  5000

/**
 * @brief Print application banner
 */
static void print_banner(void)
{
	printk("\n");
	printk("========================================\n");
	printk("  PARP-01 Custom Board Application\n");
	printk("========================================\n");
	printk("SYSCLK: 275 MHz (max safe for STM32H723)\n");
	printk("Console: USB CDC ACM\n");
	printk("LED: PE6 (TEST_LED)\n");
	printk("SW0: Inventory On/Off toggle\n");
	printk("========================================\n");
	printk("\n");
}

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

/**
 * @brief Inventory toggle callback from switch
 *
 * Called when SW0 is pressed to toggle inventory on/off.
 */
static void on_inventory_toggle(bool start)
{
	if (start) {
		/* Starting inventory - force logout for security */
		shell_login_force_logout();
		uart_router_start_inventory(&uart_router);
	} else {
		/* Stopping inventory - user can now login */
		uart_router_stop_inventory(&uart_router);
		LOG_INF("Inventory stopped. Login with 'login <password>'");
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
	int64_t last_led_toggle = 0;

	/* Configure LED GPIO first (before any USB init) */
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

	/* Initialize USB HID */
	ret = parp_usb_hid_init();
	if (ret < 0) {
		LOG_ERR("Failed to init USB HID: %d", ret);
		return ret;
	}

	/* Initialize USB device stack */
	usb_ctx = usb_device_init(usb_msg_cb);
	if (!usb_ctx) {
		LOG_ERR("Failed to initialize USB device stack");
		return -ENODEV;
	}

	/* Enable USB device */
	if (!usbd_can_detect_vbus(usb_ctx)) {
		ret = usbd_enable(usb_ctx);
		if (ret) {
			LOG_ERR("Failed to enable USB device: %d", ret);
			return ret;
		}
	}

	/* Initialize UART router */
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

	/* Initialize switch control */
	ret = switch_control_init();
	if (ret < 0) {
		LOG_WRN("Switch control init failed: %d (SW0 won't work)", ret);
	} else {
		switch_control_set_inventory_callback(on_inventory_toggle);
	}

	/* Initialize password storage (must be before shell_login_init) */
	ret = password_storage_init();
	if (ret < 0) {
		LOG_WRN("Password storage init failed: %d", ret);
	}

	/* Initialize shell login */
	ret = shell_login_init();
	if (ret < 0) {
		LOG_WRN("Shell login init failed: %d", ret);
	}

	/* Blink LED while waiting for USB host connection */
	LOG_INF("Waiting for USB host connection...");

	ret = uart_router_wait_host_connection(&uart_router, USB_HOST_WAIT_TIMEOUT_MS);
	if (ret == 0) {
		/* Host connected - print banner */
		LOG_INF("USB host connected!");
		k_msleep(100);  /* Small delay for terminal to stabilize */
		print_banner();

		/* Run E310 protocol library tests */
		e310_run_tests();
		printk("\n");

		/* Configure shell for login */
		const struct shell *sh = shell_backend_uart_get_ptr();
		if (sh) {
			shell_obscure_set(sh, true);
			shell_set_root_cmd("login");
			printk("Shell login required. Press SW0 to stop inventory first.\n");
		}
	} else {
		LOG_WRN("USB host not connected (timeout). Continuing anyway...");
	}

	/* Start E310 RFID inventory */
	ret = uart_router_start_inventory(&uart_router);
	if (ret < 0) {
		LOG_WRN("Failed to start inventory: %d (E310 may not be connected)", ret);
		/* Continue anyway - user can start later via shell */
		uart_router_set_mode(&uart_router, ROUTER_MODE_IDLE);
		switch_control_set_inventory_state(false);
	}

	LOG_INF("Starting main loop...");
	LOG_INF("Press SW0 to toggle inventory On/Off");
	LOG_INF("When inventory is Off, login with: login <password>");

	/* Main loop: blink LED and process router */
	while (1) {
		/* Process UART router */
		uart_router_process(&uart_router);

		/* Check shell login timeout */
		shell_login_check_timeout();

		/* Toggle LED every 500ms using timer-based approach */
		int64_t now = k_uptime_get();
		if ((now - last_led_toggle) >= 500) {
			last_led_toggle = now;

			led_state = !led_state;
			ret = gpio_pin_set_dt(&test_led, led_state);
			if (ret < 0) {
				LOG_WRN("Failed to set LED state: %d", ret);
			}

			/* Print status only if host is connected */
			if (uart_router_is_host_connected(&uart_router)) {
				const char *inv_status =
					switch_control_is_inventory_running() ?
					"ON" : "OFF";
				printk("[%05u] LED %s | Inventory: %s\n",
				       count++, led_state ? "ON " : "OFF",
				       inv_status);
			}
		}

		k_msleep(10);
	}

	return 0;
}
