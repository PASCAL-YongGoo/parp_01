/*
 * PARP-01 Application
 * - Console/Shell output via USART1 (PB14-TX, PB15-RX)
 * - USB HID Keyboard for EPC output
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

#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>

#include "usb_device.h"
#include "usb_hid.h"
#include "uart_router.h"
#include "switch_control.h"
#include "shell_login.h"
#include "password_storage.h"
#include "beep_control.h"
#include "rgb_led.h"
#include "e310_settings.h"

LOG_MODULE_REGISTER(parp01, LOG_LEVEL_INF);

/* E310 Protocol Library Test */
extern void e310_run_tests(void);

static struct usbd_context *usb_ctx;
static uart_router_t uart_router;

/**
 * @brief USB OTG HS pre-init workaround for STM32H723
 *
 * Performs an RCC peripheral reset of the USB OTG HS block before the
 * Zephyr UDC driver initializes it.  This clears stale register state
 * left over from a previous boot or soft-reset, which can cause
 * USB_CoreReset() inside HAL_PCD_Init() to time out waiting for the
 * GRSTCTL.AHBIDL or GRSTCTL.CSRST bits.
 *
 * This is a known issue on STM32H7 series:
 *   - https://github.com/STMicroelectronics/STM32CubeH7/issues/163
 *   - ST FAQ: "Troubleshooting a USB core soft reset stuck on an STM32"
 *
 * Must be called BEFORE usbd_enable().
 */
static void usb_otg_hs_pre_init(void)
{
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_USB1OTGHS);
	LL_AHB1_GRP1_ForceReset(LL_AHB1_GRP1_PERIPH_USB1OTGHS);
	k_busy_wait(10);
	LL_AHB1_GRP1_ReleaseReset(LL_AHB1_GRP1_PERIPH_USB1OTGHS);
	k_busy_wait(100);

	printk("USB OTG HS peripheral reset done\n");
}

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
	printk("Console: USART1 (PB14-TX, PB15-RX)\n");
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
 * - Inventory ON:  HID keyboard enabled (RFID pad mode)
 * - Inventory OFF: HID keyboard disabled (Debug mode)
 */
static void on_inventory_toggle(bool start)
{
	if (start) {
		shell_login_force_logout();
		int ret = uart_router_start_inventory(&uart_router);
		if (ret < 0) {
			switch_control_set_inventory_state(false);
			rgb_led_set_inventory_status(false);
			LOG_ERR("Inventory start failed: %d", ret);
		}
	} else {
		uart_router_stop_inventory(&uart_router);
		uart_router_set_mode(&uart_router, ROUTER_MODE_IDLE);
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
	bool last_inv_state = false;
	int64_t last_led_toggle = 0;

	/* === EARLY DEBUG: Print immediately to verify boot === */
	printk("\n\n*** PARP-01 BOOT START ***\n");

	/* Configure LED GPIO first (before any USB init) */
	if (!gpio_is_ready_dt(&test_led)) {
		printk("ERROR: LED GPIO not ready!\n");
		LOG_ERR("LED GPIO device not ready");
		k_sleep(K_FOREVER);
		return -ENODEV;
	}
	printk("LED GPIO ready\n");

	ret = gpio_pin_configure_dt(&test_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printk("ERROR: LED config failed: %d\n", ret);
		LOG_ERR("Failed to configure LED GPIO: %d", ret);
		k_sleep(K_FOREVER);
		return ret;
	}
	printk("LED configured\n");

	/* Blink LED once to show we're alive */
	gpio_pin_set_dt(&test_led, 1);
	k_msleep(200);
	gpio_pin_set_dt(&test_led, 0);
	printk("LED blinked once\n");

	/* Reset USB OTG HS peripheral BEFORE stack init to clear stale state */
	usb_otg_hs_pre_init();

	printk("Init USB HID...\n");
	ret = parp_usb_hid_init();
	if (ret < 0) {
		printk("USB HID init failed: %d (continuing anyway)\n", ret);
		LOG_ERR("Failed to init USB HID: %d", ret);
	} else {
		printk("USB HID OK\n");
	}

	printk("Init USB device stack...\n");
	usb_ctx = usb_device_init(usb_msg_cb);
	if (!usb_ctx) {
		printk("USB device init failed (continuing anyway)\n");
		LOG_ERR("Failed to initialize USB device stack");
	} else {
		printk("USB device stack OK\n");

		if (!usbd_can_detect_vbus(usb_ctx)) {
			ret = usbd_enable(usb_ctx);
			if (ret) {
				printk("USB enable failed: %d\n", ret);
				LOG_ERR("Failed to enable USB device: %d", ret);
			} else {
				printk("USB enabled\n");
			}
		}
	}

	/* Initialize UART router */
	printk("Init UART router...\n");
	ret = uart_router_init(&uart_router);
	if (ret < 0) {
		printk("UART router init failed: %d (continuing anyway)\n", ret);
		LOG_ERR("Failed to init UART router: %d", ret);
	} else {
		printk("UART router OK\n");
		ret = uart_router_start(&uart_router);
		if (ret < 0) {
			printk("UART router start failed: %d\n", ret);
			LOG_ERR("Failed to start UART router: %d", ret);
		} else {
			printk("UART router started\n");
		}
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

	ret = e310_settings_init();
	if (ret < 0) {
		LOG_WRN("E310 settings init failed: %d (using defaults)", ret);
	}

	/* Apply persisted typing speed to USB HID */
	uint16_t saved_speed = e310_settings_get_typing_speed();
	if (saved_speed >= HID_TYPING_SPEED_MIN && saved_speed <= HID_TYPING_SPEED_MAX) {
		usb_hid_set_typing_speed(saved_speed);
		LOG_INF("Typing speed loaded from EEPROM: %u CPM", saved_speed);
	}

	/* Initialize shell login - locks shell until 'login <password>' */
	/* TODO: Re-enable for production */
#if 0
	ret = shell_login_init();
	if (ret < 0) {
		LOG_WRN("Shell login init failed: %d", ret);
	}
#endif

	/* Initialize beep control */
	ret = beep_control_init();
	if (ret < 0) {
		LOG_WRN("Beep control init failed: %d (beeper won't work)", ret);
	}

	/* Initialize RGB LED */
	ret = rgb_led_init();
	if (ret < 0) {
		LOG_WRN("RGB LED init failed: %d", ret);
	} else {
		rgb_led_set_inventory_status(false);
	}

	print_banner();

	/* E310 tests disabled - was causing stack overflow
	e310_run_tests();
	printk("\n");
	*/

	/* Deferred auto-start: wait for USB HID ready, then connect E310.
	 * Must run inside main loop so uart_router_process() is active
	 * and can parse E310 responses (wait_for_e310_response depends on it). */
	bool auto_started = false;
	int64_t usb_ready_since = 0;
	const int64_t boot_time = k_uptime_get();
	#define AUTO_START_USB_SETTLE_MS  2000
	#define AUTO_START_MAX_WAIT_MS   15000

	LOG_INF("Starting main loop (auto-start after USB ready, SW0 to toggle)");

	while (1) {
		uart_router_process(&uart_router);
		rgb_led_poll();
		k_msleep(10);

		int64_t now = k_uptime_get();

		if (!auto_started) {
			bool usb_ready = usb_hid_is_ready();
			int64_t elapsed = now - boot_time;

			if (usb_ready && usb_ready_since == 0) {
				usb_ready_since = now;
				LOG_INF("USB HID ready, E310 auto-start in %d ms",
					AUTO_START_USB_SETTLE_MS);
			}

			bool usb_trigger = usb_ready_since > 0 &&
				(now - usb_ready_since) >= AUTO_START_USB_SETTLE_MS;
			bool timeout_trigger = elapsed >= AUTO_START_MAX_WAIT_MS;

			if (usb_trigger || timeout_trigger) {
				auto_started = true;
				if (timeout_trigger && !usb_trigger) {
					LOG_WRN("USB HID not ready after %lld ms, "
						 "starting E310 anyway", elapsed);
				}
				LOG_INF("Auto-starting E310 inventory...");
				ret = uart_router_start_inventory(&uart_router);
				if (ret < 0) {
					LOG_WRN("E310 auto-start failed: %d "
						 "(use SW0 or 'e310 start')", ret);
					uart_router_set_mode(&uart_router,
							     ROUTER_MODE_IDLE);
					switch_control_set_inventory_state(false);
					rgb_led_set_inventory_status(false);
				}
			}
		}

		static int64_t last_login_check = 0;
		if (now - last_login_check >= 100) {
			last_login_check = now;
			shell_login_check_timeout();
		}

		/* Toggle LED every 500ms */
		if ((now - last_led_toggle) >= 500) {
			last_led_toggle = now;

			led_state = !led_state;
			ret = gpio_pin_set_dt(&test_led, led_state);
			if (ret < 0) {
				LOG_WRN("Failed to set LED state: %d", ret);
			}
		}

		/* Print only on inventory state change */
		bool cur_inv_state = switch_control_is_inventory_running();
		if (cur_inv_state != last_inv_state) {
			last_inv_state = cur_inv_state;
			printk("Inventory: %s\n", cur_inv_state ? "ON" : "OFF");
		}
	}

	return 0;
}
