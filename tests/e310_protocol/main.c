/**
 * @file main.c
 * @brief E310 Protocol Library Unit Tests
 *
 * Tests the E310 RFID reader protocol library using Zephyr's ztest framework.
 * Runs on native_sim without real hardware.
 */

#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "e310_protocol.h"

LOG_MODULE_REGISTER(e310_test, LOG_LEVEL_DBG);

/* Test context used across tests */
static e310_context_t test_ctx;

/**
 * @brief Setup function called before each test
 */
static void *e310_test_setup(void)
{
	e310_init(&test_ctx, E310_ADDR_DEFAULT);
	return NULL;
}

/* ============================================================
 * CRC-16 Tests
 * ============================================================ */

ZTEST(e310_crc, test_crc16_calculation)
{
	uint8_t test_data[] = {0x05, 0x00, 0x50, 0x00};
	uint16_t crc = e310_crc16(test_data, sizeof(test_data));

	/* CRC should be non-zero for this data */
	zassert_not_equal(crc, 0, "CRC should not be zero");
	LOG_INF("CRC-16 of [05 00 50 00] = 0x%04X", crc);
}

ZTEST(e310_crc, test_crc16_verify_valid)
{
	/* Build a frame and verify its CRC */
	uint8_t frame[] = {0x05, 0x00, 0x50, 0x00, 0x00, 0x00};
	uint16_t crc = e310_crc16(frame, 4);
	frame[4] = (uint8_t)(crc & 0xFF);
	frame[5] = (uint8_t)((crc >> 8) & 0xFF);

	int ret = e310_verify_crc(frame, sizeof(frame));
	zassert_equal(ret, E310_OK, "Valid CRC should verify OK");
}

ZTEST(e310_crc, test_crc16_verify_invalid)
{
	uint8_t frame[] = {0x05, 0x00, 0x50, 0x00, 0xFF, 0xFF}; /* Wrong CRC */

	int ret = e310_verify_crc(frame, sizeof(frame));
	zassert_equal(ret, E310_ERR_CRC_FAILED, "Invalid CRC should fail");
}

ZTEST_SUITE(e310_crc, NULL, e310_test_setup, NULL, NULL, NULL);

/* ============================================================
 * Command Building Tests
 * ============================================================ */

ZTEST(e310_build, test_build_start_fast_inventory)
{
	size_t len = e310_build_start_fast_inventory(&test_ctx, E310_TARGET_A);

	zassert_equal(len, 6, "Start Fast Inventory should be 6 bytes");
	zassert_equal(test_ctx.tx_buffer[0], 0x05, "Length byte should be 0x05");
	zassert_equal(test_ctx.tx_buffer[2], E310_CMD_START_FAST_INVENTORY,
		      "Command should be 0x50");
	zassert_equal(test_ctx.tx_buffer[3], E310_TARGET_A,
		      "Target should be A (0x00)");

	/* Verify CRC is valid */
	int crc_valid = e310_verify_crc(test_ctx.tx_buffer, len);
	zassert_equal(crc_valid, E310_OK, "CRC should be valid");
}

ZTEST(e310_build, test_build_stop_fast_inventory)
{
	size_t len = e310_build_stop_fast_inventory(&test_ctx);

	zassert_equal(len, 5, "Stop Fast Inventory should be 5 bytes");
	zassert_equal(test_ctx.tx_buffer[2], E310_CMD_STOP_FAST_INVENTORY,
		      "Command should be 0x51");

	int crc_valid = e310_verify_crc(test_ctx.tx_buffer, len);
	zassert_equal(crc_valid, E310_OK, "CRC should be valid");
}

ZTEST(e310_build, test_build_obtain_reader_info)
{
	size_t len = e310_build_obtain_reader_info(&test_ctx);

	zassert_equal(len, 5, "Obtain Reader Info should be 5 bytes");
	zassert_equal(test_ctx.tx_buffer[2], E310_CMD_OBTAIN_READER_INFO,
		      "Command should be 0x21");

	int crc_valid = e310_verify_crc(test_ctx.tx_buffer, len);
	zassert_equal(crc_valid, E310_OK, "CRC should be valid");
}

ZTEST(e310_build, test_build_tag_inventory)
{
	e310_inventory_params_t params = {
		.q_value = 4,
		.session = E310_SESSION_S0,
		.mask_mem = E310_MEMBANK_EPC,
		.mask_addr = 0,
		.mask_len = 0,
		.tid_addr = 0,
		.tid_len = 0,
		.target = E310_TARGET_A,
		.antenna = E310_ANT_1,
		.scan_time = 10,
	};

	size_t len = e310_build_tag_inventory(&test_ctx, &params);

	zassert_true(len > 5, "Tag Inventory should have params");
	zassert_equal(test_ctx.tx_buffer[2], E310_CMD_TAG_INVENTORY,
		      "Command should be 0x01");

	int crc_valid = e310_verify_crc(test_ctx.tx_buffer, len);
	zassert_equal(crc_valid, E310_OK, "CRC should be valid");
}

ZTEST(e310_build, test_build_modify_rf_power)
{
	int len = e310_build_modify_rf_power(&test_ctx, 20);

	zassert_equal(len, 6, "Modify RF Power should be 6 bytes");
	zassert_equal(test_ctx.tx_buffer[2], E310_CMD_MODIFY_RF_POWER,
		      "Command should be 0x2F");
	zassert_equal(test_ctx.tx_buffer[3], 20, "Power should be 20 dBm");

	int crc_valid = e310_verify_crc(test_ctx.tx_buffer, len);
	zassert_equal(crc_valid, E310_OK, "CRC should be valid");
}

ZTEST(e310_build, test_build_modify_rf_power_bounds)
{
	/* Test out of bounds values */
	int len = e310_build_modify_rf_power(&test_ctx, 35);
	zassert_equal(len, E310_ERR_INVALID_PARAM, "Power > 30 should fail");

	/* Note: power is uint8_t, so -1 becomes 255 which is > 30 */
	len = e310_build_modify_rf_power(&test_ctx, 255);
	zassert_equal(len, E310_ERR_INVALID_PARAM, "Power > 30 should fail");
}

ZTEST(e310_build, test_build_simple_commands)
{
	int len;

	len = e310_build_single_tag_inventory(&test_ctx);
	zassert_equal(len, 5, "Single Tag Inventory should be 5 bytes");

	len = e310_build_obtain_reader_sn(&test_ctx);
	zassert_equal(len, 5, "Obtain Reader SN should be 5 bytes");

	len = e310_build_get_tag_count(&test_ctx);
	zassert_equal(len, 5, "Get Tag Count should be 5 bytes");

	len = e310_build_clear_memory_buffer(&test_ctx);
	zassert_equal(len, 5, "Clear Memory Buffer should be 5 bytes");

	len = e310_build_measure_temperature(&test_ctx);
	zassert_equal(len, 5, "Measure Temperature should be 5 bytes");
}

ZTEST_SUITE(e310_build, NULL, e310_test_setup, NULL, NULL, NULL);

/* ============================================================
 * Response Parsing Tests
 * ============================================================ */

ZTEST(e310_parse, test_parse_auto_upload_tag)
{
	/* Simulated auto-upload: Ant | Len | EPC | RSSI */
	uint8_t test_data[] = {
		0x80,                                         /* Antenna 1 (bitmask) */
		0x0C,                                         /* EPC length = 12 */
		0xE2, 0x00, 0x12, 0x34, 0x56, 0x78,          /* EPC data */
		0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22,
		0x45                                          /* RSSI */
	};

	e310_tag_data_t tag;
	int ret = e310_parse_auto_upload_tag(test_data, sizeof(test_data), &tag);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(tag.antenna, 0x80, "Antenna should be 0x80 (raw bitmask)");
	zassert_equal(tag.epc_len, 12, "EPC length should be 12");
	zassert_equal(tag.rssi, 0x45, "RSSI should be 0x45");
	zassert_mem_equal(tag.epc, &test_data[2], 12, "EPC data should match");
}

ZTEST(e310_parse, test_parse_auto_upload_tag_short)
{
	uint8_t short_data[] = {0x80, 0x0C}; /* Missing EPC */

	e310_tag_data_t tag;
	int ret = e310_parse_auto_upload_tag(short_data, sizeof(short_data), &tag);

	zassert_not_equal(ret, 0, "Parse should fail for short data");
}

ZTEST(e310_parse, test_parse_reader_info)
{
	/* Reader info requires 13 bytes minimum */
	uint8_t test_data[] = {
		0x02, 0x10,   /* Version: v16.2 */
		0x0E,         /* Model type */
		0xFF,         /* Protocol type */
		0x0C,         /* Max freq */
		0x00,         /* Min freq */
		0x1E,         /* Power: 30 dBm */
		0x0A,         /* Scan time */
		0x80,         /* Antenna 1 */
		0x00, 0x00,   /* Reserved */
		0x01,         /* Check antenna */
		0x00          /* Extra byte to meet 13 byte minimum */
	};

	e310_reader_info_t info;
	int ret = e310_parse_reader_info(test_data, sizeof(test_data), &info);

	zassert_equal(ret, 0, "Parse should succeed");
	zassert_equal(info.power, 30, "Power should be 30 dBm");
	zassert_equal(info.scan_time, 10, "Scan time should be 10");
}

ZTEST(e310_parse, test_format_epc_string)
{
	uint8_t epc[] = {0xE2, 0x00, 0x12, 0x34};
	char buf[32];

	int ret = e310_format_epc_string(epc, 4, buf, sizeof(buf));

	/* Format: "E2001234" (8 chars, space only added between 4-byte groups) */
	zassert_equal(ret, 8, "Formatted string should be 8 chars");
	zassert_str_equal(buf, "E2001234", "EPC format should match");
}

ZTEST(e310_parse, test_format_epc_string_buffer_too_small)
{
	uint8_t epc[] = {0xE2, 0x00, 0x12, 0x34};
	char buf[5]; /* Too small */

	int ret = e310_format_epc_string(epc, 4, buf, sizeof(buf));

	/* Implementation returns partial result, not error */
	zassert_true(ret >= 0, "Should return partial result or 0");
}

ZTEST_SUITE(e310_parse, NULL, e310_test_setup, NULL, NULL, NULL);

/* ============================================================
 * Utility Tests
 * ============================================================ */

ZTEST(e310_util, test_get_command_name)
{
	const char *name;

	name = e310_get_command_name(E310_CMD_TAG_INVENTORY);
	zassert_not_null(name, "Name should not be NULL");
	zassert_true(strlen(name) > 0, "Name should not be empty");

	name = e310_get_command_name(E310_CMD_START_FAST_INVENTORY);
	zassert_not_null(name, "Name should not be NULL");
}

ZTEST(e310_util, test_get_error_desc)
{
	const char *desc;

	desc = e310_get_error_desc(E310_OK);
	zassert_not_null(desc, "Description should not be NULL");

	desc = e310_get_error_desc(E310_ERR_CRC_FAILED);
	zassert_not_null(desc, "Description should not be NULL");

	desc = e310_get_error_desc(-999); /* Unknown error */
	zassert_not_null(desc, "Unknown error should have description");
}

ZTEST_SUITE(e310_util, NULL, e310_test_setup, NULL, NULL, NULL);
