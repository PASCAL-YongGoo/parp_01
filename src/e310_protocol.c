/**
 * @file e310_protocol.c
 * @brief Impinj E310 RFID Reader Protocol Library Implementation
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "e310_protocol.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * CRC-16 Implementation
 * ======================================================================== */

/**
 * @brief Calculate CRC-16 using CRC-16-CCITT-FALSE algorithm
 *
 * Polynomial: 0x8408 (reversed 0x1021)
 * Initial value: 0xFFFF
 * Input reflection: No
 * Output reflection: No
 * XOR output: 0x0000
 */
uint16_t e310_crc16(const uint8_t *data, size_t length)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];

		for (uint8_t bit = 0; bit < 8; bit++) {
			if (crc & 0x0001) {
				crc = (crc >> 1) ^ 0x8408;
			} else {
				crc >>= 1;
			}
		}
	}

	return crc;
}

int e310_verify_crc(const uint8_t *frame, size_t length)
{
	if (length < 3) { /* Minimum: 1 byte data + 2 bytes CRC */
		return E310_ERR_FRAME_TOO_SHORT;
	}

	/* Calculate CRC for all bytes except last 2 (which contain the CRC) */
	uint16_t calculated = e310_crc16(frame, length - 2);

	/* Extract CRC from frame (LSB first) */
	uint16_t frame_crc = frame[length - 2] | (frame[length - 1] << 8);

	return (calculated == frame_crc) ? E310_OK : E310_ERR_CRC_FAILED;
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

void e310_init(e310_context_t *ctx, uint8_t reader_addr)
{
	memset(ctx, 0, sizeof(e310_context_t));
	ctx->reader_addr = reader_addr;
}

/* ========================================================================
 * Frame Building Utilities
 * ======================================================================== */

int e310_build_frame_header(e310_context_t *ctx, uint8_t cmd, size_t data_len)
{
	/* Frame format: Len | Adr | Cmd | Data[] | CRC-16
	 * Len = 4 + data_len (includes Len, Adr, Cmd, and CRC-16)
	 */
	uint8_t frame_len = 4 + data_len;

	ctx->tx_buffer[0] = frame_len;
	ctx->tx_buffer[1] = ctx->reader_addr;
	ctx->tx_buffer[2] = cmd;

	/* Return index where data should start */
	return 3;
}

int e310_finalize_frame(e310_context_t *ctx, size_t frame_len)
{
	/* Calculate CRC for entire frame except CRC bytes themselves */
	uint16_t crc = e310_crc16(ctx->tx_buffer, frame_len);

	/* Append CRC in LSB-first order */
	ctx->tx_buffer[frame_len++] = (uint8_t)(crc & 0xFF);         /* LSB */
	ctx->tx_buffer[frame_len++] = (uint8_t)((crc >> 8) & 0xFF);  /* MSB */

	ctx->tx_len = frame_len;
	return frame_len;
}

/* ========================================================================
 * Response Parsing
 * ======================================================================== */

int e310_parse_response_header(const uint8_t *frame, size_t length,
                                 e310_response_header_t *header)
{
	/* Minimum response: Len + Adr + reCmd + Status + CRC-16 = 5 bytes */
	if (length < 5) {
		return -1; /* Frame too short */
	}

	/* Verify CRC */
	int crc_result = e310_verify_crc(frame, length);
	if (crc_result != E310_OK) {
		return crc_result; /* CRC error */
	}

	/* Parse header */
	header->len = frame[0];
	header->addr = frame[1];
	header->recmd = frame[2];
	header->status = frame[3];

	/* Verify length field matches actual length */
	if (header->len != length) {
		return -3; /* Length mismatch */
	}

	return 0; /* Success */
}

/* ========================================================================
 * Command Builders - Fast Inventory (Priority)
 * ======================================================================== */

int e310_build_start_fast_inventory(e310_context_t *ctx, uint8_t target)
{
	/* Frame: Len(0x05) | Adr | Cmd(0x50) | Target | CRC-16 */
	size_t idx = e310_build_frame_header(ctx, E310_CMD_START_FAST_INVENTORY, 1);

	ctx->tx_buffer[idx++] = target; /* 0x00=A, 0x01=B */

	return e310_finalize_frame(ctx, idx);
}

int e310_build_stop_fast_inventory(e310_context_t *ctx)
{
	/* Frame: Len(0x04) | Adr | Cmd(0x51) | CRC-16 */
	size_t idx = e310_build_frame_header(ctx, E310_CMD_STOP_FAST_INVENTORY, 0);

	return e310_finalize_frame(ctx, idx);
}

/* ========================================================================
 * Command Builders - Standard Inventory
 * ======================================================================== */

int e310_build_tag_inventory(e310_context_t *ctx,
                                 const e310_inventory_params_t *params)
{
	/* Calculate mask data length in bytes */
	uint8_t mask_data_len = (params->mask_len + 7) / 8;

	/* Data length = 8 + mask_data_len + optional fields */
	size_t data_len = 8 + mask_data_len;

	/* Optional fields (if provided in params) */
	bool has_target = true;    /* Always include for simplicity */
	bool has_antenna = (params->antenna != 0);
	bool has_scan_time = (params->scan_time != 0);

	if (has_target) data_len++;
	if (has_antenna) data_len++;
	if (has_scan_time) data_len++;

	size_t idx = e310_build_frame_header(ctx, E310_CMD_TAG_INVENTORY, data_len);

	/* Build data payload */
	ctx->tx_buffer[idx++] = params->q_value;
	ctx->tx_buffer[idx++] = params->session;
	ctx->tx_buffer[idx++] = params->mask_mem;
	ctx->tx_buffer[idx++] = (uint8_t)(params->mask_addr >> 8);   /* MSB */
	ctx->tx_buffer[idx++] = (uint8_t)(params->mask_addr & 0xFF); /* LSB */
	ctx->tx_buffer[idx++] = params->mask_len;

	/* Copy mask data */
	if (mask_data_len > 0) {
		memcpy(&ctx->tx_buffer[idx], params->mask_data, mask_data_len);
		idx += mask_data_len;
	}

	ctx->tx_buffer[idx++] = params->tid_addr;
	ctx->tx_buffer[idx++] = params->tid_len;

	/* Optional fields */
	if (has_target) {
		ctx->tx_buffer[idx++] = params->target;
	}
	if (has_antenna) {
		ctx->tx_buffer[idx++] = params->antenna;
	}
	if (has_scan_time) {
		ctx->tx_buffer[idx++] = params->scan_time;
	}

	return e310_finalize_frame(ctx, idx);
}

/* ========================================================================
 * Command Builders - Reader Configuration
 * ======================================================================== */

int e310_build_obtain_reader_info(e310_context_t *ctx)
{
	/* Frame: Len(0x04) | Adr | Cmd(0x21) | CRC-16 */
	size_t idx = e310_build_frame_header(ctx, E310_CMD_OBTAIN_READER_INFO, 0);

	return e310_finalize_frame(ctx, idx);
}

int e310_build_stop_immediately(e310_context_t *ctx)
{
	/* Frame: Len(0x04) | Adr | Cmd(0x93) | CRC-16 */
	size_t idx = e310_build_frame_header(ctx, E310_CMD_STOP_IMMEDIATELY, 0);

	return e310_finalize_frame(ctx, idx);
}

/* ========================================================================
 * Response Parsers - Tag Data
 * ======================================================================== */

int e310_parse_tag_data(const uint8_t *data, size_t length, e310_tag_data_t *tag)
{
	if (length < 2) {
		return -1; /* Minimum: data_length + at least 1 byte data */
	}

	/* Clear tag structure */
	memset(tag, 0, sizeof(e310_tag_data_t));

	size_t idx = 0;

	/* Parse data length byte */
	uint8_t data_len_byte = data[idx++];

	bool epc_tid_combined = (data_len_byte & 0x80) != 0;  /* bit 7 */
	bool phase_freq_present = (data_len_byte & 0x40) != 0; /* bit 6 */
	uint8_t data_bytes = data_len_byte & 0x3F;              /* bits 5:0 */

	if (idx + data_bytes > length) {
		return -2; /* Not enough data */
	}

	/* Parse EPC/TID data */
	if (epc_tid_combined && data_bytes >= 2) {
		/*
		 * Data contains both EPC and TID
		 * Format: [PC(2)] + [EPC(variable)] + [CRC(2)] + [TID(variable)]
		 *
		 * PC Word structure (first 2 bytes):
		 *   - bits 15-11: EPC length in words (multiply by 2 for bytes)
		 *   - bits 10-8: User memory indicator
		 *   - bits 7-0: Additional info (XPC, etc.)
		 *
		 * Total EPC block = PC(2) + EPC(variable) + CRC(2)
		 */
		uint16_t pc_word = ((uint16_t)data[idx] << 8) | data[idx + 1];
		uint8_t epc_words = (pc_word >> 11) & 0x1F;  /* bits 15-11 */
		uint8_t epc_bytes = epc_words * 2;           /* Convert to bytes */

		/* EPC block size = PC(2) + EPC + CRC(2) */
		uint8_t epc_block_size = 2 + epc_bytes + 2;

		if (epc_block_size <= data_bytes) {
			/* Copy EPC (skip PC, include actual EPC data) */
			tag->epc_len = epc_bytes;
			if (tag->epc_len > E310_MAX_EPC_LENGTH) {
				tag->epc_len = E310_MAX_EPC_LENGTH;
			}
			memcpy(tag->epc, &data[idx + 2], tag->epc_len);

			/* Copy TID (remaining data after EPC block) */
			uint8_t tid_offset = epc_block_size;
			uint8_t tid_len = data_bytes - tid_offset;
			if (tid_len > 0) {
				if (tid_len > E310_MAX_TID_LENGTH) {
					tid_len = E310_MAX_TID_LENGTH;
				}
				memcpy(tag->tid, &data[idx + tid_offset], tid_len);
				tag->tid_len = tid_len;
				tag->has_tid = true;
			}
		} else {
			/* Fallback: treat all data as EPC */
			tag->epc_len = data_bytes;
			if (tag->epc_len > E310_MAX_EPC_LENGTH) {
				tag->epc_len = E310_MAX_EPC_LENGTH;
			}
			memcpy(tag->epc, &data[idx], tag->epc_len);
			tag->has_tid = false;
		}
	} else {
		/* Data contains only EPC */
		tag->epc_len = data_bytes;
		if (tag->epc_len > E310_MAX_EPC_LENGTH) {
			tag->epc_len = E310_MAX_EPC_LENGTH;
		}
		memcpy(tag->epc, &data[idx], tag->epc_len);
		tag->has_tid = false;
	}
	idx += data_bytes;

	/* Parse RSSI */
	if (idx >= length) {
		return -3; /* Missing RSSI */
	}
	tag->rssi = data[idx++];

	/* Parse phase (if present) */
	if (phase_freq_present) {
		if (idx + 4 > length) {
			return -4; /* Missing phase data */
		}
		tag->phase = (uint32_t)data[idx] |
		             ((uint32_t)data[idx + 1] << 8) |
		             ((uint32_t)data[idx + 2] << 16) |
		             ((uint32_t)data[idx + 3] << 24);
		idx += 4;
		tag->has_phase = true;

		/* Parse frequency (if present) */
		if (idx + 3 <= length) {
			tag->frequency_khz = (uint32_t)data[idx] |
			                     ((uint32_t)data[idx + 1] << 8) |
			                     ((uint32_t)data[idx + 2] << 16);
			idx += 3;
			tag->has_frequency = true;
		}
	}

	return (int)idx; /* Return number of bytes consumed */
}

int e310_parse_auto_upload_tag(const uint8_t *data, size_t length,
                                 e310_tag_data_t *tag)
{
	/* Auto-upload format: Ant | Len | EPC/TID | RSSI */
	if (length < 3) {
		return -1; /* Minimum: Ant + Len + at least 1 byte data + RSSI */
	}

	/* Clear tag structure */
	memset(tag, 0, sizeof(e310_tag_data_t));

	size_t idx = 0;

	/* Parse antenna */
	tag->antenna = data[idx++];

	/* Parse EPC/TID length */
	uint8_t epc_len = data[idx++];

	if (idx + epc_len + 1 > length) {
		return -2; /* Not enough data */
	}

	/* Parse EPC/TID data */
	tag->epc_len = epc_len;
	if (tag->epc_len > E310_MAX_EPC_LENGTH) {
		tag->epc_len = E310_MAX_EPC_LENGTH;
	}
	memcpy(tag->epc, &data[idx], tag->epc_len);
	idx += epc_len;

	/* Parse RSSI */
	tag->rssi = data[idx++];

	return 0; /* Success */
}

/* ========================================================================
 * Response Parsers - Statistics and Info
 * ======================================================================== */

int e310_parse_inventory_stats(const uint8_t *data, size_t length,
                                 e310_inventory_stats_t *stats)
{
	/* Statistics format: Ant | ReadRate(2) | TotalCount(4) */
	if (length < 7) {
		return -1;
	}

	stats->antenna = data[0];
	stats->read_rate = data[1] | (data[2] << 8);
	stats->total_count = (uint32_t)data[3] |
	                     ((uint32_t)data[4] << 8) |
	                     ((uint32_t)data[5] << 16) |
	                     ((uint32_t)data[6] << 24);

	return 0;
}

int e310_parse_reader_info(const uint8_t *data, size_t length,
                             e310_reader_info_t *info)
{
	/* Reader info format: Version(2) | Type | Tr_Type | dmaxfre | dminfre |
	 *                      Power | Scntm | Ant | Reserved(2) | CheckAnt */
	if (length < 13) {
		return -1;
	}

	info->firmware_version = data[0] | (data[1] << 8);
	info->model_type = data[2];
	info->protocol_type = data[3];
	info->max_freq = data[4];
	info->min_freq = data[5];
	info->power = data[6];
	info->scan_time = data[7];
	info->antenna = data[8];
	/* data[9], data[10] = reserved */
	info->check_antenna = data[11];

	return 0;
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

const char *e310_get_command_name(uint8_t cmd)
{
	switch (cmd) {
	case E310_CMD_TAG_INVENTORY:           return "Tag Inventory";
	case E310_CMD_READ_DATA:               return "Read Data";
	case E310_CMD_WRITE_DATA:              return "Write Data";
	case E310_CMD_WRITE_EPC:               return "Write EPC";
	case E310_CMD_KILL_TAG:                return "Kill Tag";
	case E310_CMD_SINGLE_TAG_INVENTORY:    return "Single Tag Inventory";
	case E310_CMD_OBTAIN_READER_INFO:      return "Obtain Reader Info";
	case E310_CMD_MODIFY_RF_POWER:         return "Modify RF Power";
	case E310_CMD_START_FAST_INVENTORY:    return "Start Fast Inventory";
	case E310_CMD_STOP_FAST_INVENTORY:     return "Stop Fast Inventory";
	case E310_CMD_STOP_IMMEDIATELY:        return "Stop Immediately";
	case E310_RECMD_AUTO_UPLOAD:           return "Auto-Upload Tag";
	default:                               return "Unknown Command";
	}
}

const char *e310_get_status_desc(uint8_t status)
{
	switch (status) {
	case E310_STATUS_SUCCESS:              return "Success";
	case E310_STATUS_OPERATION_COMPLETE:   return "Operation Complete";
	case E310_STATUS_INVENTORY_TIMEOUT:    return "Inventory Timeout";
	case E310_STATUS_MORE_DATA:            return "More Data";
	case E310_STATUS_MEMORY_FULL:          return "Memory Full";
	case E310_STATUS_STATISTICS_DATA:      return "Statistics Data";
	case E310_STATUS_ANTENNA_ERROR:        return "Antenna Error";
	case E310_STATUS_INVALID_LENGTH:       return "Invalid Length";
	case E310_STATUS_INVALID_COMMAND_CRC:  return "Invalid Command/CRC";
	case E310_STATUS_UNKNOWN_PARAMETER:    return "Unknown Parameter";
	default:                               return "Unknown Status";
	}
}

const char *e310_get_error_desc(int error_code)
{
	switch (error_code) {
	case E310_OK:                    return "Success";
	case E310_ERR_FRAME_TOO_SHORT:   return "Frame too short";
	case E310_ERR_CRC_FAILED:        return "CRC verification failed";
	case E310_ERR_LENGTH_MISMATCH:   return "Length field mismatch";
	case E310_ERR_BUFFER_OVERFLOW:   return "Buffer overflow";
	case E310_ERR_INVALID_PARAM:     return "Invalid parameter";
	case E310_ERR_MISSING_DATA:      return "Missing required data";
	case E310_ERR_PARSE_ERROR:       return "Parse error";
	default:                         return "Unknown error";
	}
}

int e310_format_epc_string(const uint8_t *epc, uint8_t epc_len,
                            char *output, size_t output_size)
{
	if (epc_len == 0 || output_size < 3) {
		return 0;
	}

	size_t written = 0;

	for (uint8_t i = 0; i < epc_len && written + 3 < output_size; i++) {
		int ret = snprintf(&output[written], output_size - written,
		                   "%02X", epc[i]);
		if (ret > 0) {
			written += ret;
		}

		/* Add space every 4 bytes for readability */
		if ((i + 1) % 4 == 0 && i < epc_len - 1 && written + 2 < output_size) {
			output[written++] = ' ';
		}
	}

	output[written] = '\0';
	return (int)written;
}

/* ========================================================================
 * Command Builders - Read/Write Operations
 * ======================================================================== */

int e310_build_read_data(e310_context_t *ctx, const e310_read_params_t *params)
{
	if (!ctx || !params) {
		return E310_ERR_INVALID_PARAM;
	}

	if (params->word_count == 0 || params->word_count > 120) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t data_len;
	bool use_mask = (params->epc_len == 0 || params->epc_len == 0xFF);

	if (use_mask) {
		/* Mask mode: ENum(1) + Mem(1) + WordPtr(1) + Num(1) + Pwd(4) + MaskMem(1) + MaskAdr(2) + MaskLen(1) + MaskData */
		uint8_t mask_bytes = (params->mask_len + 7) / 8;
		data_len = 1 + 1 + 1 + 1 + 4 + 1 + 2 + 1 + mask_bytes;
	} else {
		/* EPC mode: ENum(1) + EPC + Mem(1) + WordPtr(1) + Num(1) + Pwd(4) */
		data_len = 1 + params->epc_len + 1 + 1 + 1 + 4;
	}

	if (3 + data_len + 2 > E310_MAX_FRAME_SIZE) {
		return E310_ERR_BUFFER_OVERFLOW;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_READ_DATA, data_len);

	if (use_mask) {
		/* Mask mode */
		ctx->tx_buffer[idx++] = 0xFF;  /* ENum = 0xFF indicates mask mode */
		ctx->tx_buffer[idx++] = params->mem_bank;
		ctx->tx_buffer[idx++] = params->word_ptr;
		ctx->tx_buffer[idx++] = params->word_count;
		memcpy(&ctx->tx_buffer[idx], params->password, 4);
		idx += 4;
		ctx->tx_buffer[idx++] = params->mask_mem;
		ctx->tx_buffer[idx++] = (uint8_t)(params->mask_addr >> 8);   /* MSB */
		ctx->tx_buffer[idx++] = (uint8_t)(params->mask_addr & 0xFF); /* LSB */
		ctx->tx_buffer[idx++] = params->mask_len;
		uint8_t mask_bytes = (params->mask_len + 7) / 8;
		if (mask_bytes > 0) {
			memcpy(&ctx->tx_buffer[idx], params->mask_data, mask_bytes);
			idx += mask_bytes;
		}
	} else {
		/* EPC mode */
		uint8_t epc_words = (params->epc_len + 1) / 2;  /* Convert to word count */
		ctx->tx_buffer[idx++] = epc_words;
		memcpy(&ctx->tx_buffer[idx], params->epc, params->epc_len);
		idx += params->epc_len;
		ctx->tx_buffer[idx++] = params->mem_bank;
		ctx->tx_buffer[idx++] = params->word_ptr;
		ctx->tx_buffer[idx++] = params->word_count;
		memcpy(&ctx->tx_buffer[idx], params->password, 4);
		idx += 4;
	}

	return e310_finalize_frame(ctx, idx);
}

int e310_build_write_data(e310_context_t *ctx, const e310_write_params_t *params)
{
	if (!ctx || !params) {
		return E310_ERR_INVALID_PARAM;
	}

	if (params->word_count == 0 || params->word_count > 120) {
		return E310_ERR_INVALID_PARAM;
	}

	/* Data: WNum(1) + ENum(1) + EPC + Mem(1) + WordPtr(1) + WriteData + Pwd(4) */
	uint8_t epc_words = (params->epc_len + 1) / 2;
	uint8_t write_bytes = params->word_count * 2;
	size_t data_len = 1 + 1 + params->epc_len + 1 + 1 + write_bytes + 4;

	if (3 + data_len + 2 > E310_MAX_FRAME_SIZE) {
		return E310_ERR_BUFFER_OVERFLOW;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_WRITE_DATA, data_len);

	ctx->tx_buffer[idx++] = params->word_count;  /* WNum */
	ctx->tx_buffer[idx++] = epc_words;           /* ENum */
	memcpy(&ctx->tx_buffer[idx], params->epc, params->epc_len);
	idx += params->epc_len;
	ctx->tx_buffer[idx++] = params->mem_bank;
	ctx->tx_buffer[idx++] = params->word_ptr;
	memcpy(&ctx->tx_buffer[idx], params->data, write_bytes);
	idx += write_bytes;
	memcpy(&ctx->tx_buffer[idx], params->password, 4);
	idx += 4;

	return e310_finalize_frame(ctx, idx);
}

int e310_build_write_epc(e310_context_t *ctx, const e310_write_epc_params_t *params)
{
	if (!ctx || !params) {
		return E310_ERR_INVALID_PARAM;
	}

	/* Data: ENum(1) + OldEPC + NewEPCLen(1) + NewEPC + Pwd(4) */
	uint8_t old_epc_words = (params->old_epc_len + 1) / 2;
	uint8_t new_epc_words = (params->new_epc_len + 1) / 2;
	size_t data_len = 1 + params->old_epc_len + 1 + params->new_epc_len + 4;

	if (3 + data_len + 2 > E310_MAX_FRAME_SIZE) {
		return E310_ERR_BUFFER_OVERFLOW;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_WRITE_EPC, data_len);

	ctx->tx_buffer[idx++] = old_epc_words;
	memcpy(&ctx->tx_buffer[idx], params->old_epc, params->old_epc_len);
	idx += params->old_epc_len;
	ctx->tx_buffer[idx++] = new_epc_words;
	memcpy(&ctx->tx_buffer[idx], params->new_epc, params->new_epc_len);
	idx += params->new_epc_len;
	memcpy(&ctx->tx_buffer[idx], params->password, 4);
	idx += 4;

	return e310_finalize_frame(ctx, idx);
}

int e310_build_modify_rf_power(e310_context_t *ctx, uint8_t power)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	if (power > 30) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_MODIFY_RF_POWER, 1);
	ctx->tx_buffer[idx++] = power;

	return e310_finalize_frame(ctx, idx);
}

int e310_build_select(e310_context_t *ctx, const e310_select_params_t *params)
{
	if (!ctx || !params) {
		return E310_ERR_INVALID_PARAM;
	}

	/* Data: SelParam(1) + Truncate(1) + Target(1) + Action(1) + MemBank(1) + Pointer(2) + Length(1) + Mask */
	uint8_t mask_bytes = (params->mask_len + 7) / 8;
	size_t data_len = 1 + 1 + 1 + 1 + 1 + 2 + 1 + mask_bytes;

	if (3 + data_len + 2 > E310_MAX_FRAME_SIZE) {
		return E310_ERR_BUFFER_OVERFLOW;
	}

	size_t idx = e310_build_frame_header(ctx, 0x9A, data_len);

	ctx->tx_buffer[idx++] = params->sel_param;
	ctx->tx_buffer[idx++] = params->truncate;
	ctx->tx_buffer[idx++] = params->target;
	ctx->tx_buffer[idx++] = params->action;
	ctx->tx_buffer[idx++] = params->mem_bank;
	ctx->tx_buffer[idx++] = (uint8_t)(params->pointer >> 8);   /* MSB */
	ctx->tx_buffer[idx++] = (uint8_t)(params->pointer & 0xFF); /* LSB */
	ctx->tx_buffer[idx++] = params->mask_len;
	if (mask_bytes > 0) {
		memcpy(&ctx->tx_buffer[idx], params->mask, mask_bytes);
		idx += mask_bytes;
	}

	return e310_finalize_frame(ctx, idx);
}

/* ========================================================================
 * Command Builders - Simple Commands
 * ======================================================================== */

int e310_build_single_tag_inventory(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_SINGLE_TAG_INVENTORY, 0);
	return e310_finalize_frame(ctx, idx);
}

int e310_build_obtain_reader_sn(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_OBTAIN_READER_SN, 0);
	return e310_finalize_frame(ctx, idx);
}

int e310_build_get_data_from_buffer(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_GET_DATA_FROM_BUFFER, 0);
	return e310_finalize_frame(ctx, idx);
}

int e310_build_clear_memory_buffer(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_CLEAR_MEMORY_BUFFER, 0);
	return e310_finalize_frame(ctx, idx);
}

int e310_build_get_tag_count(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, E310_CMD_GET_TAG_COUNT_FROM_BUFFER, 0);
	return e310_finalize_frame(ctx, idx);
}

int e310_build_measure_temperature(e310_context_t *ctx)
{
	if (!ctx) {
		return E310_ERR_INVALID_PARAM;
	}

	size_t idx = e310_build_frame_header(ctx, 0x92, 0);
	return e310_finalize_frame(ctx, idx);
}

/* ========================================================================
 * Response Parsers - Additional
 * ======================================================================== */

int e310_parse_read_response(const uint8_t *data, size_t length,
                               e310_read_response_t *response)
{
	if (!data || !response) {
		return E310_ERR_INVALID_PARAM;
	}

	memset(response, 0, sizeof(e310_read_response_t));

	if (length == 0) {
		return E310_ERR_MISSING_DATA;
	}

	/* Data is word-aligned, copy directly */
	size_t bytes_to_copy = length;
	if (bytes_to_copy > sizeof(response->data)) {
		bytes_to_copy = sizeof(response->data);
	}

	memcpy(response->data, data, bytes_to_copy);
	response->word_count = (bytes_to_copy + 1) / 2;

	return E310_OK;
}

int e310_parse_tag_count(const uint8_t *data, size_t length, uint32_t *count)
{
	if (!data || !count) {
		return E310_ERR_INVALID_PARAM;
	}

	if (length < 2) {
		return E310_ERR_FRAME_TOO_SHORT;
	}

	/* Tag count is 2 bytes, little-endian */
	*count = data[0] | ((uint32_t)data[1] << 8);

	return E310_OK;
}

int e310_parse_temperature(const uint8_t *data, size_t length, int8_t *temperature)
{
	if (!data || !temperature) {
		return E310_ERR_INVALID_PARAM;
	}

	if (length < 1) {
		return E310_ERR_FRAME_TOO_SHORT;
	}

	/* Temperature is signed 8-bit value */
	*temperature = (int8_t)data[0];

	return E310_OK;
}
