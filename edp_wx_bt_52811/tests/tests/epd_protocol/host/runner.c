/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>

static int failures;
static int tests_run;

int host_assert_equal(long long actual, long long expected, const char *expression,
		      const char *file, int line)
{
	if (actual == expected) {
		return 1;
	}

	fprintf(stderr, "%s:%d: %s (actual=%lld expected=%lld)\n", file, line,
		expression, actual, expected);
	++failures;
	return 0;
}

int host_assert_equal_ptr(const void *actual, const void *expected,
			  const char *expression, const char *file, int line)
{
	if (actual == expected) {
		return 1;
	}

	fprintf(stderr, "%s:%d: %s (actual=%p expected=%p)\n", file, line,
		expression, actual, expected);
	++failures;
	return 0;
}

int host_assert_is_null(const void *actual, const char *expression,
			const char *file, int line)
{
	return host_assert_equal_ptr(actual, NULL, expression, file, line);
}

#define DECLARE_TEST(suite, name) void suite##_##name(void)
#define RUN_TEST(suite, name)                                                   \
	do {                                                                      \
		const int failures_before = failures;                               \
		++tests_run;                                                         \
		suite##_##name();                                                     \
		printf("%s %s.%s\n", failures == failures_before ? "PASS" :      \
		       "FAIL", #suite, #name);                                     \
	} while (0)

DECLARE_TEST(epd_protocol_crc16, test_reference_vectors);
DECLARE_TEST(epd_protocol_plain, test_accepts_full_plain_transfer);
DECLARE_TEST(epd_protocol_plain, test_rejects_overflow_and_aborts);
DECLARE_TEST(epd_protocol_plain, test_rejects_continue_in_idle_and_early_refresh);
DECLARE_TEST(epd_protocol_plain, test_rejects_null_arguments);
DECLARE_TEST(epd_protocol_block, test_accepts_all_blocks_in_order);
DECLARE_TEST(epd_protocol_block, test_crc_mismatch_drops_block_but_keeps_session);
DECLARE_TEST(epd_protocol_block, test_duplicate_block_not_written);
DECLARE_TEST(epd_protocol_block, test_rejects_bad_block_id_and_total);
DECLARE_TEST(epd_protocol_block, test_reset_transfer_clears_state);
DECLARE_TEST(epd_protocol_misc, test_query_status_builds_a1_response);
DECLARE_TEST(epd_protocol_misc, test_basic_commands_produce_expected_actions);
DECLARE_TEST(epd_protocol_misc, test_unknown_and_ignored_commands);

int main(void)
{
	RUN_TEST(epd_protocol_crc16, test_reference_vectors);
	RUN_TEST(epd_protocol_plain, test_accepts_full_plain_transfer);
	RUN_TEST(epd_protocol_plain, test_rejects_overflow_and_aborts);
	RUN_TEST(epd_protocol_plain, test_rejects_continue_in_idle_and_early_refresh);
	RUN_TEST(epd_protocol_plain, test_rejects_null_arguments);
	RUN_TEST(epd_protocol_block, test_accepts_all_blocks_in_order);
	RUN_TEST(epd_protocol_block, test_crc_mismatch_drops_block_but_keeps_session);
	RUN_TEST(epd_protocol_block, test_duplicate_block_not_written);
	RUN_TEST(epd_protocol_block, test_rejects_bad_block_id_and_total);
	RUN_TEST(epd_protocol_block, test_reset_transfer_clears_state);
	RUN_TEST(epd_protocol_misc, test_query_status_builds_a1_response);
	RUN_TEST(epd_protocol_misc, test_basic_commands_produce_expected_actions);
	RUN_TEST(epd_protocol_misc, test_unknown_and_ignored_commands);

	printf("%d tests, %d failures\n", tests_run, failures);
	return failures == 0 ? 0 : 1;
}
