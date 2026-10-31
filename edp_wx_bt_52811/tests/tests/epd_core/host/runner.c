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

int host_assert_true(int actual, const char *expression, const char *file, int line)
{
	return host_assert_equal(actual, 1, expression, file, line);
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

DECLARE_TEST(epd_core_init, test_exact_sequence);
DECLARE_TEST(epd_core_init, test_rejects_invalid_arguments_without_bus_access);
DECLARE_TEST(epd_core_init, test_propagates_each_injected_bus_error_and_stops);
DECLARE_TEST(epd_core_init, test_powered_state_can_sleep_after_final_wait_failure);
DECLARE_TEST(epd_core_stream, test_rejects_calls_before_initialization);
DECLARE_TEST(epd_core_stream, test_begin_and_chunked_write_track_exact_bytes);
DECLARE_TEST(epd_core_stream, test_rejects_invalid_and_overflow_writes_without_mutation);
DECLARE_TEST(epd_core_stream, test_failed_begin_and_write_do_not_change_state);
DECLARE_TEST(epd_core_stream, test_rejects_out_of_order_calls_without_bus_access);
DECLARE_TEST(epd_core_refresh, test_requires_exact_image_and_writes_exact_sequence);
DECLARE_TEST(epd_core_refresh, test_rejects_incomplete_image_without_bus_access);
DECLARE_TEST(epd_core_refresh, test_each_bus_error_stops_sequence_and_allows_retry);
DECLARE_TEST(epd_core_sleep, test_writes_exact_sequence);
DECLARE_TEST(epd_core_sleep, test_each_bus_error_stops_sequence);
#ifdef HOST_TEST
DECLARE_TEST(epd_adapter, test_all_apis_reject_calls_before_initialization);
DECLARE_TEST(epd_adapter, test_init_drives_spi_gpio_and_busy_polling);
DECLARE_TEST(epd_adapter, test_init_propagates_busy_read_error_and_timeout);
DECLARE_TEST(epd_adapter, test_failed_abort_requires_reinitialization);
DECLARE_TEST(epd_adapter, test_abort_recovers_powered_init_failure);
#endif

int main(void)
{
	RUN_TEST(epd_core_init, test_exact_sequence);
	RUN_TEST(epd_core_init, test_rejects_invalid_arguments_without_bus_access);
	RUN_TEST(epd_core_init, test_propagates_each_injected_bus_error_and_stops);
	RUN_TEST(epd_core_init,
		 test_powered_state_can_sleep_after_final_wait_failure);
	RUN_TEST(epd_core_stream, test_rejects_calls_before_initialization);
	RUN_TEST(epd_core_stream, test_begin_and_chunked_write_track_exact_bytes);
	RUN_TEST(epd_core_stream,
		 test_rejects_invalid_and_overflow_writes_without_mutation);
	RUN_TEST(epd_core_stream, test_failed_begin_and_write_do_not_change_state);
	RUN_TEST(epd_core_stream,
		 test_rejects_out_of_order_calls_without_bus_access);
	RUN_TEST(epd_core_refresh,
		 test_requires_exact_image_and_writes_exact_sequence);
	RUN_TEST(epd_core_refresh,
		 test_rejects_incomplete_image_without_bus_access);
	RUN_TEST(epd_core_refresh,
		 test_each_bus_error_stops_sequence_and_allows_retry);
	RUN_TEST(epd_core_sleep, test_writes_exact_sequence);
	RUN_TEST(epd_core_sleep, test_each_bus_error_stops_sequence);
#ifdef HOST_TEST
	RUN_TEST(epd_adapter, test_all_apis_reject_calls_before_initialization);
	RUN_TEST(epd_adapter, test_init_drives_spi_gpio_and_busy_polling);
	RUN_TEST(epd_adapter, test_init_propagates_busy_read_error_and_timeout);
	RUN_TEST(epd_adapter, test_failed_abort_requires_reinitialization);
	RUN_TEST(epd_adapter, test_abort_recovers_powered_init_failure);
#endif

	printf("%d tests, %d failures\n", tests_run, failures);
	return failures == 0 ? 0 : 1;
}
