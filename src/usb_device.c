/**
 * @file usb_device.c
 * @brief USB Device Stack (NEXT) initialization helpers
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "usb_device.h"
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_device, LOG_LEVEL_INF);

#define PARP_USB_VID 0x2FE3
#define PARP_USB_PID 0x0001

USBD_DEVICE_DEFINE(parp_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   PARP_USB_VID, PARP_USB_PID);

USBD_DESC_LANG_DEFINE(parp_lang);
USBD_DESC_STRING_DEFINE(parp_mfr, "PARP", 1);
USBD_DESC_STRING_DEFINE(parp_product, "PARP-01", 2);
USBD_DESC_STRING_DEFINE(parp_sn, "PARP01-0001", 3);

USBD_DESC_CONFIG_DEFINE(parp_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(parp_hs_cfg_desc, "HS Configuration");

static const uint8_t attributes = 0;

USBD_CONFIGURATION_DEFINE(parp_fs_config, attributes, 100, &parp_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(parp_hs_config, attributes, 100, &parp_hs_cfg_desc);

static void parp_fix_code_triple(struct usbd_context *ctx,
				 const enum usbd_speed speed)
{
#if defined(CONFIG_USBD_CDC_ACM_CLASS)
	usbd_device_set_code_triple(ctx, speed,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
#else
	usbd_device_set_code_triple(ctx, speed, 0, 0, 0);
#endif
}

struct usbd_context *usb_device_init(usbd_msg_cb_t msg_cb)
{
	int err;

	err = usbd_add_descriptor(&parp_usbd, &parp_lang);
	if (err) {
		LOG_ERR("Failed to add language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&parp_usbd, &parp_mfr);
	if (err) {
		LOG_ERR("Failed to add manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&parp_usbd, &parp_product);
	if (err) {
		LOG_ERR("Failed to add product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&parp_usbd, &parp_sn);
	if (err) {
		LOG_ERR("Failed to add serial descriptor (%d)", err);
		return NULL;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&parp_usbd) == USBD_SPEED_HS) {
		err = usbd_add_configuration(&parp_usbd, USBD_SPEED_HS,
					     &parp_hs_config);
		if (err) {
			LOG_ERR("Failed to add HS configuration");
			return NULL;
		}

		err = usbd_register_all_classes(&parp_usbd, USBD_SPEED_HS, 1, NULL);
		if (err) {
			LOG_ERR("Failed to register HS classes");
			return NULL;
		}

		parp_fix_code_triple(&parp_usbd, USBD_SPEED_HS);
	}

	err = usbd_add_configuration(&parp_usbd, USBD_SPEED_FS, &parp_fs_config);
	if (err) {
		LOG_ERR("Failed to add FS configuration");
		return NULL;
	}

	err = usbd_register_all_classes(&parp_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("Failed to register FS classes");
		return NULL;
	}

	parp_fix_code_triple(&parp_usbd, USBD_SPEED_FS);
	usbd_self_powered(&parp_usbd, attributes & USB_SCD_SELF_POWERED);

	if (msg_cb) {
		err = usbd_msg_register_cb(&parp_usbd, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
	}

	err = usbd_init(&parp_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB device (%d)", err);
		return NULL;
	}

	return &parp_usbd;
}
