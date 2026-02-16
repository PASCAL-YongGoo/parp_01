/**
 * @file e310_test.c
 * @brief E310 Protocol Library Test and Examples
 *
 * This file contains test code and usage examples for the E310 protocol library.
 * It demonstrates how to build commands, parse responses, and handle tag data.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "e310_protocol.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(e310_test, LOG_LEVEL_DBG);

/**
 * @brief Print hex dump of buffer
 */
static void print_hex_dump(const char *label, const uint8_t *data, size_t length)
{
	LOG_INF("%s (%zu bytes):", label, length);

	char line[80];
	size_t pos = 0;

	for (size_t i = 0; i < length; i++) {
		pos += snprintf(&line[pos], sizeof(line) - pos, "%02X ", data[i]);

		if ((i + 1) % 16 == 0 || i == length - 1) {
			LOG_INF("  %s", line);
			pos = 0;
		}
	}
}

/**
 * @brief Test CRC-16 calculation
 */
static void test_crc16(void)
{
	LOG_INF("=== Testing CRC-16 ===");

	/* Test vector: simple frame without CRC */
	uint8_t test_data[] = {0x05, 0x00, 0x50, 0x00};
	uint16_t crc = e310_crc16(test_data, sizeof(test_data));

	LOG_INF("Test data: 05 00 50 00");
	LOG_INF("Calculated CRC-16: 0x%04X", crc);
	LOG_INF("CRC bytes (LSB,MSB): 0x%02X 0x%02X", (uint8_t)(crc & 0xFF),
	        (uint8_t)((crc >> 8) & 0xFF));

	/* Test verification */
	uint8_t test_frame[] = {0x05, 0x00, 0x50, 0x00, 0x00, 0x00}; /* Placeholder CRC */
	test_frame[4] = (uint8_t)(crc & 0xFF);
	test_frame[5] = (uint8_t)((crc >> 8) & 0xFF);

	int valid = e310_verify_crc(test_frame, sizeof(test_frame));
	LOG_INF("CRC verification: %s", (valid == E310_OK) ? "PASS" : "FAIL");
}

/**
 * @brief Test building Start Fast Inventory command
 */
static void test_build_start_fast_inventory(void)
{
	LOG_INF("=== Testing Start Fast Inventory Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	size_t len = e310_build_start_fast_inventory(&ctx, E310_TARGET_A);

	print_hex_dump("Start Fast Inventory (Target A)", ctx.tx_buffer, len);

	/* Expected frame:
	 * Len: 0x05
	 * Adr: 0x00
	 * Cmd: 0x50
	 * Target: 0x00
	 * CRC-16: (calculated)
	 */
	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_START_FAST_INVENTORY));
	LOG_INF("Frame length: %zu bytes", len);
}

/**
 * @brief Test building Stop Fast Inventory command
 */
static void test_build_stop_fast_inventory(void)
{
	LOG_INF("=== Testing Stop Fast Inventory Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	size_t len = e310_build_stop_fast_inventory(&ctx);

	print_hex_dump("Stop Fast Inventory", ctx.tx_buffer, len);

	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_STOP_FAST_INVENTORY));
	LOG_INF("Frame length: %zu bytes", len);
}

/**
 * @brief Test building Obtain Reader Info command
 */
static void test_build_obtain_reader_info(void)
{
	LOG_INF("=== Testing Obtain Reader Info Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	size_t len = e310_build_obtain_reader_info(&ctx);

	print_hex_dump("Obtain Reader Info", ctx.tx_buffer, len);

	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_OBTAIN_READER_INFO));
	LOG_INF("Frame length: %zu bytes", len);
}

/**
 * @brief Test building Tag Inventory command
 */
static void test_build_tag_inventory(void)
{
	LOG_INF("=== Testing Tag Inventory Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	/* Build inventory parameters */
	e310_inventory_params_t params = {
		.q_value = 4,                /* Q=4, no flags */
		.session = E310_SESSION_S0,
		.mask_mem = E310_MEMBANK_EPC,
		.mask_addr = 0,
		.mask_len = 0,               /* No mask */
		.tid_addr = 0,
		.tid_len = 0,                /* No TID */
		.target = E310_TARGET_A,
		.antenna = E310_ANT_1,       /* Antenna 1 */
		.scan_time = 10,             /* 1 second (10 * 100ms) */
	};

	size_t len = e310_build_tag_inventory(&ctx, &params);

	print_hex_dump("Tag Inventory (Q=4, No Mask)", ctx.tx_buffer, len);

	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_TAG_INVENTORY));
	LOG_INF("Frame length: %zu bytes", len);
}

/**
 * @brief Test parsing auto-upload tag data
 */
static void test_parse_auto_upload_tag(void)
{
	LOG_INF("=== Testing Auto-Upload Tag Parsing ===");

	/* Simulated auto-upload data: Ant | Len | EPC | RSSI */
	uint8_t test_data[] = {
		0x80,                                   /* Antenna 1 */
		0x0C,                                   /* EPC length = 12 bytes */
		0xE2, 0x00, 0x12, 0x34, 0x56, 0x78,    /* EPC data (example) */
		0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22,
		0x45                                    /* RSSI */
	};

	e310_tag_data_t tag;
	int ret = e310_parse_auto_upload_tag(test_data, sizeof(test_data), &tag);

	if (ret == 0) {
		LOG_INF("Parse successful!");
		LOG_INF("  Antenna: %u", tag.antenna);
		LOG_INF("  EPC Length: %u bytes", tag.epc_len);
		LOG_INF("  RSSI: %u", tag.rssi);

		char epc_str[128];
		e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
		LOG_INF("  EPC: %s", epc_str);
	} else {
		LOG_ERR("Parse failed: %d", ret);
	}
}

/**
 * @brief Test parsing reader info response
 */
static void test_parse_reader_info(void)
{
	LOG_INF("=== Testing Reader Info Parsing ===");

	/* Simulated reader info response data */
	uint8_t test_data[] = {
		0x02, 0x10,     /* Version: v16.2 */
		0x0E,           /* Model type */
		0xFF,           /* Protocol type */
		0x0C,           /* Max freq */
		0x00,           /* Min freq */
		0x1E,           /* Power: 30 dBm */
		0x0A,           /* Scan time */
		0x80,           /* Antenna 1 */
		0x00, 0x00,     /* Reserved */
		0x01            /* Check antenna enabled */
	};

	e310_reader_info_t info;
	int ret = e310_parse_reader_info(test_data, sizeof(test_data), &info);

	if (ret == 0) {
		LOG_INF("Parse successful!");
		LOG_INF("  Firmware Version: %u.%u",
		        (info.firmware_version >> 8) & 0xFF,
		        info.firmware_version & 0xFF);
		LOG_INF("  Model Type: 0x%02X", info.model_type);
		LOG_INF("  RF Power: %u dBm", info.power);
		LOG_INF("  Scan Time: %u", info.scan_time);
	} else {
		LOG_ERR("Parse failed: %d", ret);
	}
}

/**
 * @brief Test building Read Data command
 */
static void test_build_read_data(void)
{
	LOG_INF("=== Testing Read Data Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	e310_read_params_t params = {
		.epc = {0xE2, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22},
		.epc_len = 12,
		.mem_bank = E310_MEMBANK_USER,
		.word_ptr = 0,
		.word_count = 4,
		.password = {0x00, 0x00, 0x00, 0x00},
	};

	int len = e310_build_read_data(&ctx, &params);

	print_hex_dump("Read Data (User Bank, 4 words)", ctx.tx_buffer, len);
	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_READ_DATA));
	LOG_INF("Frame length: %d bytes", len);
}

/**
 * @brief Test building Write Data command
 */
static void test_build_write_data(void)
{
	LOG_INF("=== Testing Write Data Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	e310_write_params_t params = {
		.epc = {0xE2, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22},
		.epc_len = 12,
		.mem_bank = E310_MEMBANK_USER,
		.word_ptr = 0,
		.data = {0x11, 0x22, 0x33, 0x44},
		.word_count = 2,
		.password = {0x00, 0x00, 0x00, 0x00},
	};

	int len = e310_build_write_data(&ctx, &params);

	print_hex_dump("Write Data (User Bank, 2 words)", ctx.tx_buffer, len);
	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_WRITE_DATA));
	LOG_INF("Frame length: %d bytes", len);
}

/**
 * @brief Test building Modify RF Power command
 */
static void test_build_modify_rf_power(void)
{
	LOG_INF("=== Testing Modify RF Power Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	int len = e310_build_modify_rf_power(&ctx, 20);

	print_hex_dump("Modify RF Power (20 dBm)", ctx.tx_buffer, len);
	LOG_INF("Command: %s", e310_get_command_name(E310_CMD_MODIFY_RF_POWER));
	LOG_INF("Frame length: %d bytes", len);
}

/**
 * @brief Test building Select command
 */
static void test_build_select(void)
{
	LOG_INF("=== Testing Select Command ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	e310_select_params_t params = {
		.antenna = 0x00,
		.target = E310_TARGET_A,
		.action = 0x00,
		.mem_bank = E310_MEMBANK_EPC,
		.pointer = 0x20,   /* Start at EPC data (skip PC+CRC) */
		.mask_len = 96,    /* 12 bytes = 96 bits */
		.mask = {0xE2, 0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x11, 0x22},
		.truncate = 0x00,
	};

	int len = e310_build_select(&ctx, &params);

	print_hex_dump("Select (EPC mask)", ctx.tx_buffer, len);
	LOG_INF("Frame length: %d bytes", len);
}

/**
 * @brief Test building simple commands
 */
static void test_build_simple_commands(void)
{
	LOG_INF("=== Testing Simple Commands ===");

	e310_context_t ctx;
	e310_init(&ctx, E310_ADDR_DEFAULT);

	int len;

	len = e310_build_single_tag_inventory(&ctx);
	LOG_INF("Single Tag Inventory: %d bytes", len);

	len = e310_build_obtain_reader_sn(&ctx);
	LOG_INF("Obtain Reader SN: %d bytes", len);

	len = e310_build_get_tag_count(&ctx);
	LOG_INF("Get Tag Count: %d bytes", len);

	len = e310_build_clear_memory_buffer(&ctx);
	LOG_INF("Clear Memory Buffer: %d bytes", len);

	len = e310_build_measure_temperature(&ctx);
	LOG_INF("Measure Temperature: %d bytes", len);
}

/**
 * @brief Test error description function
 */
static void test_error_descriptions(void)
{
	LOG_INF("=== Testing Error Descriptions ===");

	LOG_INF("E310_OK: %s", e310_get_error_desc(E310_OK));
	LOG_INF("E310_ERR_CRC_FAILED: %s", e310_get_error_desc(E310_ERR_CRC_FAILED));
	LOG_INF("E310_ERR_BUFFER_OVERFLOW: %s", e310_get_error_desc(E310_ERR_BUFFER_OVERFLOW));
	LOG_INF("Unknown error (-99): %s", e310_get_error_desc(-99));
}

/**
 * @brief Run all E310 protocol library tests
 */
void e310_run_tests(void)
{
	LOG_INF("========================================");
	LOG_INF("  E310 Protocol Library Tests (v2)");
	LOG_INF("========================================\n");

	test_crc16();
	LOG_INF("");

	test_build_start_fast_inventory();
	LOG_INF("");

	test_build_stop_fast_inventory();
	LOG_INF("");

	test_build_obtain_reader_info();
	LOG_INF("");

	test_build_tag_inventory();
	LOG_INF("");

	test_parse_auto_upload_tag();
	LOG_INF("");

	test_parse_reader_info();
	LOG_INF("");

	/* New tests for v2 */
	test_build_read_data();
	LOG_INF("");

	test_build_write_data();
	LOG_INF("");

	test_build_modify_rf_power();
	LOG_INF("");

	test_build_select();
	LOG_INF("");

	test_build_simple_commands();
	LOG_INF("");

	test_error_descriptions();
	LOG_INF("");

	LOG_INF("========================================");
	LOG_INF("  All tests completed");
	LOG_INF("========================================\n");
}
