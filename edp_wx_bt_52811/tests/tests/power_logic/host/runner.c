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

int host_assert_false(int actual, const char *expression, const char *file, int line)
{
	return host_assert_equal(actual, 0, expression, file, line);
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

DECLARE_TEST(battery_state, test_exact_threshold_boundaries);
DECLARE_TEST(battery_state, test_macro_relative_boundaries);
DECLARE_TEST(led_policy, test_receiving_has_priority_over_low_battery);
DECLARE_TEST(led_policy, test_low_pattern_exact_steps);
DECLARE_TEST(led_policy, test_short_alert_interval_never_underflows);
DECLARE_TEST(led_policy, test_deadline_remaining_clamps_safely);
DECLARE_TEST(app_led, test_adapter_lifecycle_and_mode_handoffs);
DECLARE_TEST(app_led, test_low_battery_deadline_accounts_for_late_handlers);
DECLARE_TEST(app_led, test_force_off_ignores_setter_during_cancel);
DECLARE_TEST(app_battery, test_adapter_lifecycle_sampling_and_suspend);

int main(void)
{
	RUN_TEST(battery_state, test_exact_threshold_boundaries);
	RUN_TEST(battery_state, test_macro_relative_boundaries);
	RUN_TEST(led_policy, test_receiving_has_priority_over_low_battery);
	RUN_TEST(led_policy, test_low_pattern_exact_steps);
	RUN_TEST(led_policy, test_short_alert_interval_never_underflows);
	RUN_TEST(led_policy, test_deadline_remaining_clamps_safely);
	RUN_TEST(app_led, test_adapter_lifecycle_and_mode_handoffs);
	RUN_TEST(app_led, test_low_battery_deadline_accounts_for_late_handlers);
	RUN_TEST(app_led, test_force_off_ignores_setter_during_cancel);
	RUN_TEST(app_battery, test_adapter_lifecycle_sampling_and_suspend);

	printf("%d tests, %d failures\n", tests_run, failures);
	return failures == 0 ? 0 : 1;
}
