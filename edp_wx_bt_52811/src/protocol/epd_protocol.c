/* SPDX-License-Identifier: Apache-2.0 */
/*
 * edp_html 兼容分块协议实现（纯逻辑，不依赖硬件）。
 * 普通传图（0x30）：首块 cfg 高 4 位 = 0（复位会话、进入 RAM 写模式），
 *                   续块 cfg 高 4 位 = 0xF0（顺序累计写入，越界拒绝并中止）；
 * CRC 块传图（0x31）：block_id/total_blocks 均小端 2B，payload 尾部附
 *                     CRC16-CCITT（反射 0x8408、初值 0xFFFF）小端 2B；
 *                     CRC 通过置位 bitmap，失败丢弃该块（标记缺失）；
 * REFRESH（0x05）：普通模式累计满 10000 或块模式 received==total 才允许刷新。
 */

#include "epd_protocol.h"

#include <stdbool.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_le16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

uint16_t epd_protocol_crc16(const uint8_t *data, size_t length)
{
	uint16_t crc = 0xFFFFU;

	while (length-- > 0U) {
		uint8_t i;

		crc ^= *data++;
		for (i = 0U; i < 8U; i++) {
			crc = (uint16_t)((crc >> 1) ^
				((crc & 1U) ? 0x8408U : 0U));
		}
	}

	return crc;
}

void epd_transfer_reset(struct epd_transfer *transfer)
{
	if (transfer == NULL) {
		return;
	}

	transfer->state = EPD_TRANSFER_IDLE;
	transfer->bytes_written = 0U;
	transfer->total_blocks = 0U;
	transfer->received_blocks = 0U;
	transfer->session_id = 0U;
	transfer->image_length = EPD_IMAGE_LENGTH;
	memset(transfer->block_bitmap, 0, sizeof(transfer->block_bitmap));
}

static void clear_bitmap(struct epd_transfer *transfer)
{
	memset(transfer->block_bitmap, 0, sizeof(transfer->block_bitmap));
	transfer->received_blocks = 0U;
}

static bool bitmap_isset(const struct epd_transfer *transfer, uint16_t block_id)
{
	return (transfer->block_bitmap[block_id / 8U] &
		(1U << (block_id % 8U))) != 0U;
}

static void bitmap_set(struct epd_transfer *transfer, uint16_t block_id)
{
	transfer->block_bitmap[block_id / 8U] |= (uint8_t)(1U << (block_id % 8U));
	transfer->received_blocks++;
}

static void abort_session(struct epd_transfer *transfer, uint8_t *action)
{
	transfer->state = EPD_TRANSFER_IDLE;
	clear_bitmap(transfer);
	transfer->bytes_written = 0U;
	if (action != NULL) {
		*action = EPD_ACTION_ABORT;
	}
}

static int handle_write_img(struct epd_transfer *transfer, const uint8_t *packet,
			    size_t length, const uint8_t **payload,
			    size_t *payload_length, uint8_t *action)
{
	const uint8_t cfg = packet[1];
	const bool first = (cfg & EPD_CFG_FIRST_MASK) == EPD_CFG_FIRST_BLOCK;
	const uint8_t *data = &packet[2];
	const size_t data_length = length - 2U;

	*payload = data;
	*payload_length = data_length;

	if (first) {
		/* 首块：复位会话并进入接收态（覆盖任何进行中的旧会话） */
		transfer->state = EPD_TRANSFER_RECEIVING;
		transfer->bytes_written = 0U;
		clear_bitmap(transfer);
		transfer->total_blocks = 0U;

		if (data_length > transfer->image_length) {
			abort_session(transfer, action);
			return EPD_PROTOCOL_BAD_LENGTH;
		}

		transfer->bytes_written = data_length;
		*action = EPD_ACTION_START_IMAGE;
		return EPD_PROTOCOL_OK;
	}

	/* 续块 */
	if (transfer->state != EPD_TRANSFER_RECEIVING) {
		*payload = NULL;
		*payload_length = 0U;
		*action = EPD_ACTION_NONE;
		return EPD_PROTOCOL_BAD_STATE;
	}

	if (data_length == 0U ||
	    transfer->bytes_written > transfer->image_length ||
	    data_length > transfer->image_length - transfer->bytes_written) {
		abort_session(transfer, action);
		return EPD_PROTOCOL_BAD_LENGTH;
	}

	transfer->bytes_written += data_length;
	*action = EPD_ACTION_WRITE;
	return EPD_PROTOCOL_OK;
}

static int handle_write_block(struct epd_transfer *transfer, const uint8_t *packet,
			      size_t length, const uint8_t **payload,
			      size_t *payload_length, uint8_t *action)
{
	const uint16_t block_id = read_le16(&packet[1]);
	const uint16_t total_blocks = read_le16(&packet[3]);
	const uint8_t cfg = packet[5];
	const bool first = (cfg & EPD_CFG_FIRST_MASK) == EPD_CFG_FIRST_BLOCK;
	const uint8_t *data = &packet[EPD_BLOCK_HEADER_LENGTH];
	const size_t data_length = length - EPD_BLOCK_HEADER_LENGTH -
				  EPD_BLOCK_CRC_LENGTH;

	*payload = data;
	*payload_length = data_length;

	if (first) {
		/* 首块：复位会话（total_blocks 以本包声明为准） */
		transfer->state = EPD_TRANSFER_RECEIVING;
		transfer->bytes_written = 0U;
		clear_bitmap(transfer);
		transfer->total_blocks = total_blocks;
		*action = EPD_ACTION_START_IMAGE;
	} else if (transfer->state != EPD_TRANSFER_RECEIVING) {
		*payload = NULL;
		*payload_length = 0U;
		*action = EPD_ACTION_NONE;
		return EPD_PROTOCOL_BAD_STATE;
	}

	if (total_blocks == 0U || block_id >= total_blocks ||
	    block_id >= EPD_PROTOCOL_MAX_BLOCKS) {
		*payload = NULL;
		*payload_length = 0U;
		return EPD_PROTOCOL_BAD_LENGTH;
	}

	/* 校验 payload CRC16-CCITT（包尾 2B LE） */
	if (epd_protocol_crc16(data, data_length) !=
	    read_le16(&packet[EPD_BLOCK_HEADER_LENGTH + data_length])) {
		/* CRC 失败：丢弃本块（bitmap 保持缺失），会话继续 */
		*payload = NULL;
		*payload_length = 0U;
		*action = EPD_ACTION_NONE;
		return EPD_PROTOCOL_BLOCK_DROPPED;
	}

	if (!bitmap_isset(transfer, block_id)) {
		bitmap_set(transfer, block_id);
		transfer->bytes_written += data_length;
		if (action != NULL && *action == EPD_ACTION_START_IMAGE) {
			/* 首块也计入已收 */
			*action = EPD_ACTION_START_IMAGE;
		} else {
			*action = EPD_ACTION_WRITE;
		}
	} else {
		/* 重复块：不写屏（payload 丢弃），会话保持 */
		*payload = NULL;
		*payload_length = 0U;
		*action = EPD_ACTION_NONE;
	}

	return EPD_PROTOCOL_OK;
}

int epd_protocol_handle(struct epd_transfer *transfer, const uint8_t *packet,
			size_t length, const uint8_t **payload,
			size_t *payload_length, uint8_t *action)
{
	if (transfer == NULL || packet == NULL || payload == NULL ||
	    payload_length == NULL || action == NULL) {
		return EPD_PROTOCOL_BAD_LENGTH;
	}

	*payload = NULL;
	*payload_length = 0U;
	*action = EPD_ACTION_NONE;

	if (length == 0U) {
		return EPD_PROTOCOL_BAD_LENGTH;
	}

	switch (packet[0]) {
	case EPD_CMD_SET_PINS:
		/* 网页端连接后回填引脚配置；本设备引脚固定，仅记录 */
		return EPD_PROTOCOL_IGNORED;

	case EPD_CMD_INIT:
		*action = EPD_ACTION_INIT;
		return EPD_PROTOCOL_OK;

	case EPD_CMD_CLEAR:
		*action = EPD_ACTION_CLEAR;
		return EPD_PROTOCOL_OK;

	case EPD_CMD_SEND_CMD:
	case EPD_CMD_SEND_DATA:
		/* 透传屏幕命令：本设备驱动已固定时序，忽略 */
		return EPD_PROTOCOL_IGNORED;

	case EPD_CMD_REFRESH:
		if (transfer->state != EPD_TRANSFER_RECEIVING) {
			return EPD_PROTOCOL_BAD_STATE;
		}
		if (transfer->total_blocks > 0U) {
			if (transfer->received_blocks != transfer->total_blocks) {
				return EPD_PROTOCOL_BAD_LENGTH;
			}
		} else if (transfer->bytes_written != transfer->image_length) {
			return EPD_PROTOCOL_BAD_LENGTH;
		}
		transfer->state = EPD_TRANSFER_COMPLETE;
		*action = EPD_ACTION_REFRESH;
		return EPD_PROTOCOL_OK;

	case EPD_CMD_SLEEP:
		*action = EPD_ACTION_SLEEP;
		return EPD_PROTOCOL_OK;

	case EPD_CMD_SET_TIME:
	case EPD_CMD_SET_WEEK_START:
		/* 时间功能未启用，接受但忽略 */
		return EPD_PROTOCOL_IGNORED;

	case EPD_CMD_WRITE_IMG:
		if (length < 2U) {
			return EPD_PROTOCOL_BAD_LENGTH;
		}
		return handle_write_img(transfer, packet, length, payload,
					payload_length, action);

	case EPD_CMD_WRITE_BLOCK:
		if (length < EPD_BLOCK_HEADER_LENGTH + EPD_BLOCK_CRC_LENGTH) {
			return EPD_PROTOCOL_BAD_LENGTH;
		}
		return handle_write_block(transfer, packet, length, payload,
					  payload_length, action);

	case EPD_CMD_QUERY_STATUS:
		*action = EPD_ACTION_QUERY_STATUS;
		return EPD_PROTOCOL_OK;

	case EPD_CMD_RESET_TRANSFER:
		if (length != 2U) {
			return EPD_PROTOCOL_BAD_LENGTH;
		}
		transfer->session_id = packet[1];
		transfer->state = EPD_TRANSFER_IDLE;
		transfer->bytes_written = 0U;
		clear_bitmap(transfer);
		return EPD_PROTOCOL_OK;

	default:
		/* 未知命令：忽略，不打断进行中的会话 */
		return EPD_PROTOCOL_IGNORED;
	}
}

size_t epd_protocol_build_status(struct epd_transfer *transfer, uint8_t *out,
				 size_t capacity)
{
	size_t bitmap_bytes;
	size_t i;

	if (transfer == NULL || out == NULL) {
		return 0U;
	}

	bitmap_bytes = (transfer->total_blocks + 7U) / 8U;
	if (bitmap_bytes > EPD_PROTOCOL_BITMAP_BYTES) {
		bitmap_bytes = EPD_PROTOCOL_BITMAP_BYTES;
	}

	if (capacity < 1U + 2U + 2U + 1U + 1U + bitmap_bytes) {
		return 0U;
	}

	out[0] = 0xA1U;
	write_le16(&out[1], transfer->total_blocks);
	write_le16(&out[3], transfer->received_blocks);
	out[5] = (uint8_t)transfer->session_id;
	out[6] = (transfer->state == EPD_TRANSFER_RECEIVING) ? 1U : 0U;
	for (i = 0U; i < bitmap_bytes; i++) {
		out[7 + i] = transfer->block_bitmap[i];
	}

	return 1U + 2U + 2U + 1U + 1U + bitmap_bytes;
}
