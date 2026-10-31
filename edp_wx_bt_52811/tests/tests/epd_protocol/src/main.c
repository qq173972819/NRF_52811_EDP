/* SPDX-License-Identifier: Apache-2.0 */
/*
 * epd_protocol 新协议（edp_html 兼容）测试：
 *   CRC16-CCITT 基准向量、0x30 普通传图、0x31 CRC 块传图、
 *   0x32 状态查询、0x33 重置、错误/边界路径。
 * 纯逻辑，可在 native_sim 上运行。
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "protocol/epd_protocol.h"

/* ---- CRC16-CCITT ---- */
ZTEST(epd_protocol_crc16, test_reference_vectors)
{
	static const uint8_t vector[] = "123456789";
	static const uint8_t aa10[10] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
					  0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
	static const uint8_t zeros2[2] = { 0x00, 0x00 };

	zassert_equal(epd_protocol_crc16(vector, sizeof(vector) - 1U), 0x6F91U);
	zassert_equal(epd_protocol_crc16(NULL, 0U), 0xFFFFU);
	zassert_equal(epd_protocol_crc16(aa10, sizeof(aa10)), 0x7634U);
	zassert_equal(epd_protocol_crc16(zeros2, sizeof(zeros2)), 0xF0B8U);
}

/* ---- 0x30 普通传图 ---- */
ZTEST(epd_protocol_plain, test_accepts_full_plain_transfer)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_PROTOCOL_MAX_PAYLOAD + 8];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	size_t sent = 0U;
	int result;

	epd_transfer_reset(&transfer);

	/* 首块：cfg 高 4 位 = 0（首块） */
	packet[0] = EPD_CMD_WRITE_IMG;
	packet[1] = EPD_CFG_LAYER_COLOR;
	result = epd_protocol_handle(&transfer, packet, 2U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_START_IMAGE);
	zassert_equal(payload_length, 0U);

	/* 分段发送 10000 字节 */
	while (sent < EPD_IMAGE_LENGTH) {
		const size_t chunk =
			MIN(EPD_PROTOCOL_MAX_PAYLOAD, EPD_IMAGE_LENGTH - sent);

		memset(packet, 0x5A, 2U + chunk);
		packet[0] = EPD_CMD_WRITE_IMG;
		packet[1] = EPD_CFG_CONTINUE | EPD_CFG_LAYER_COLOR;
		result = epd_protocol_handle(&transfer, packet, 2U + chunk,
					     &payload, &payload_length, &action);
		zassert_equal(result, EPD_PROTOCOL_OK);
		zassert_equal(action, EPD_ACTION_WRITE);
		zassert_equal(payload_length, chunk);
		sent += chunk;
	}

	/* 满 10000 后允许刷新 */
	packet[0] = EPD_CMD_REFRESH;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_REFRESH);
}

ZTEST(epd_protocol_plain, test_rejects_overflow_and_aborts)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_PROTOCOL_MAX_PAYLOAD + 8];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	packet[0] = EPD_CMD_WRITE_IMG;
	packet[1] = EPD_CFG_LAYER_COLOR;
	result = epd_protocol_handle(&transfer, packet, 2U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);

	/* 续块超过剩余空间 */
	memset(packet, 0x11, 2U + EPD_PROTOCOL_MAX_PAYLOAD);
	packet[0] = EPD_CMD_WRITE_IMG;
	packet[1] = EPD_CFG_CONTINUE | EPD_CFG_LAYER_COLOR;
	result = epd_protocol_handle(&transfer, packet,
				     2U + EPD_PROTOCOL_MAX_PAYLOAD, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);
	zassert_equal(action, EPD_ACTION_ABORT);
	zassert_equal(transfer.state, EPD_TRANSFER_IDLE);
}

ZTEST(epd_protocol_plain, test_rejects_continue_in_idle_and_early_refresh)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_PROTOCOL_MAX_PAYLOAD + 8];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	/* IDLE 状态收续块 */
	memset(packet, 0x22, 4U);
	packet[0] = EPD_CMD_WRITE_IMG;
	packet[1] = EPD_CFG_CONTINUE | EPD_CFG_LAYER_COLOR;
	result = epd_protocol_handle(&transfer, packet, 4U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_STATE);

	/* 未满 10000 刷新被拒绝 */
	epd_transfer_reset(&transfer);
	packet[0] = EPD_CMD_WRITE_IMG;
	packet[1] = EPD_CFG_LAYER_COLOR;
	result = epd_protocol_handle(&transfer, packet, 2U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);

	packet[0] = EPD_CMD_REFRESH;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);
	zassert_equal(transfer.state, EPD_TRANSFER_RECEIVING);
}

ZTEST(epd_protocol_plain, test_rejects_null_arguments)
{
	int result = epd_protocol_handle(NULL, NULL, 0U, NULL, NULL, NULL);

	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);
}

/* ---- 0x31 CRC 块传图 ---- */
static void append_block_crc(uint8_t *packet, size_t data_length)
{
	uint16_t crc = epd_protocol_crc16(&packet[EPD_BLOCK_HEADER_LENGTH],
					  data_length);

	packet[EPD_BLOCK_HEADER_LENGTH + data_length] = (uint8_t)crc;
	packet[EPD_BLOCK_HEADER_LENGTH + data_length + 1U] =
		(uint8_t)(crc >> 8);
}

ZTEST(epd_protocol_block, test_accepts_all_blocks_in_order)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_BLOCK_HEADER_LENGTH + EPD_PROTOCOL_MAX_PAYLOAD +
		       EPD_BLOCK_CRC_LENGTH];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;
	uint16_t block_id;

	epd_transfer_reset(&transfer);

	for (block_id = 0U; block_id < 4U; block_id++) {
		const size_t data_length = 20U;

		memset(packet, (uint8_t)(0x30U + block_id),
		       EPD_BLOCK_HEADER_LENGTH + data_length + EPD_BLOCK_CRC_LENGTH);
		packet[0] = EPD_CMD_WRITE_BLOCK;
		packet[1] = (uint8_t)block_id;
		packet[2] = (uint8_t)(block_id >> 8);
		packet[3] = 4U;
		packet[4] = 0U;
		packet[5] = (block_id == 0U) ? EPD_CFG_LAYER_COLOR
					     : (EPD_CFG_CONTINUE | EPD_CFG_LAYER_COLOR);
		append_block_crc(packet, data_length);

		result = epd_protocol_handle(&transfer, packet,
					     EPD_BLOCK_HEADER_LENGTH +
						     data_length +
						     EPD_BLOCK_CRC_LENGTH,
					     &payload, &payload_length, &action);
		zassert_equal(result, EPD_PROTOCOL_OK);
		if (block_id == 0U) {
			zassert_equal(action, EPD_ACTION_START_IMAGE);
		} else {
			zassert_equal(action, EPD_ACTION_WRITE);
		}
		zassert_equal(payload_length, data_length);
	}

	zassert_equal(transfer.received_blocks, 4U);
	zassert_equal(transfer.total_blocks, 4U);

	packet[0] = EPD_CMD_REFRESH;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_REFRESH);
}

ZTEST(epd_protocol_block, test_crc_mismatch_drops_block_but_keeps_session)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_BLOCK_HEADER_LENGTH + 20U + EPD_BLOCK_CRC_LENGTH];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	memset(packet, 0x44, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 2U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	packet[EPD_BLOCK_HEADER_LENGTH + 20U] ^= 0xFFU; /* 破坏 CRC */

	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BLOCK_DROPPED);
	zassert_equal(action, EPD_ACTION_NONE);
	zassert_equal(transfer.received_blocks, 0U);
	zassert_equal(transfer.state, EPD_TRANSFER_RECEIVING);

	/* 会话仍可继续：补发正确块后刷新成功 */
	memset(packet, 0x44, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 2U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(transfer.received_blocks, 1U);
}

ZTEST(epd_protocol_block, test_duplicate_block_not_written)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_BLOCK_HEADER_LENGTH + 20U + EPD_BLOCK_CRC_LENGTH];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	memset(packet, 0x55, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 1U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(transfer.received_blocks, 1U);

	/* 同一块重发：CRC 通过但不再写屏（payload 置 NULL/0） */
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(payload_length, 0U);
	zassert_is_null(payload);
	zassert_equal(transfer.received_blocks, 1U);
}

ZTEST(epd_protocol_block, test_rejects_bad_block_id_and_total)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_BLOCK_HEADER_LENGTH + 20U + EPD_BLOCK_CRC_LENGTH];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	memset(packet, 0x66, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 9U;	 /* block_id >= total(2) */
	packet[2] = 0U;
	packet[3] = 2U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);

	/* total = 0 拒绝 */
	memset(packet, 0x66, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 0U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);
}

ZTEST(epd_protocol_block, test_reset_transfer_clears_state)
{
	struct epd_transfer transfer;
	uint8_t packet[EPD_BLOCK_HEADER_LENGTH + 20U + EPD_BLOCK_CRC_LENGTH];
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	epd_transfer_reset(&transfer);

	memset(packet, 0x77, sizeof(packet));
	packet[0] = EPD_CMD_WRITE_BLOCK;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 1U;
	packet[4] = 0U;
	packet[5] = EPD_CFG_LAYER_COLOR;
	append_block_crc(packet, 20U);
	result = epd_protocol_handle(&transfer, packet, sizeof(packet),
				     &payload, &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(transfer.state, EPD_TRANSFER_RECEIVING);

	/* 0x33 RESET_TRANSFER（带 session id） */
	packet[0] = EPD_CMD_RESET_TRANSFER;
	packet[1] = 0xAB;
	result = epd_protocol_handle(&transfer, packet, 2U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(transfer.state, EPD_TRANSFER_IDLE);
	zassert_equal(transfer.received_blocks, 0U);
	zassert_equal(transfer.session_id, 0xABU);
}

/* ---- 状态查询 / 基础命令 ---- */
ZTEST(epd_protocol_misc, test_query_status_builds_a1_response)
{
	struct epd_transfer transfer;
	uint8_t status[EPD_PROTOCOL_MAX_STATUS_LENGTH];
	size_t length;

	epd_transfer_reset(&transfer);
	transfer.total_blocks = 4U;
	transfer.received_blocks = 2U;
	transfer.state = EPD_TRANSFER_RECEIVING;
	transfer.session_id = 3U;
	transfer.block_bitmap[0] = 0x05U; /* block 0 与 2 已收 */

	length = epd_protocol_build_status(&transfer, status, sizeof(status));
	zassert_equal(length, 12U); /* 1+2+2+1+1+ceil(4/8)=1 */
	zassert_equal(status[0], 0xA1U);
	zassert_equal(status[1], 4U);
	zassert_equal(status[3], 2U);
	zassert_equal(status[5], 3U);
	zassert_equal(status[6], 1U);
	zassert_equal(status[7], 0x05U);
}

ZTEST(epd_protocol_misc, test_basic_commands_produce_expected_actions)
{
	struct epd_transfer transfer;
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	uint8_t packet[2];
	int result;

	epd_transfer_reset(&transfer);

	packet[0] = EPD_CMD_INIT;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_INIT);

	packet[0] = EPD_CMD_CLEAR;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_CLEAR);

	packet[0] = EPD_CMD_SLEEP;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_SLEEP);

	packet[0] = EPD_CMD_QUERY_STATUS;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_OK);
	zassert_equal(action, EPD_ACTION_QUERY_STATUS);
}

ZTEST(epd_protocol_misc, test_unknown_and_ignored_commands)
{
	struct epd_transfer transfer;
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	uint8_t packet[4];
	int result;

	epd_transfer_reset(&transfer);

	/* SET_PINS：接受但忽略 */
	packet[0] = EPD_CMD_SET_PINS;
	packet[1] = 0U;
	packet[2] = 0U;
	packet[3] = 0U;
	result = epd_protocol_handle(&transfer, packet, 4U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_IGNORED);
	zassert_equal(action, EPD_ACTION_NONE);

	/* 未知命令 */
	packet[0] = 0x7FU;
	result = epd_protocol_handle(&transfer, packet, 1U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_IGNORED);

	/* 空包 */
	result = epd_protocol_handle(&transfer, NULL, 0U, &payload,
				     &payload_length, &action);
	zassert_equal(result, EPD_PROTOCOL_BAD_LENGTH);
}
