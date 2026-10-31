/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ZEPHYR_ZTEST_H_
#define POWER_LOGIC_HOST_ZEPHYR_ZTEST_H_

int host_assert_equal(long long actual, long long expected, const char *expression,
		      const char *file, int line);
int host_assert_true(int actual, const char *expression, const char *file, int line);
int host_assert_false(int actual, const char *expression, const char *file, int line);

#define ZTEST(suite, name) void suite##_##name(void)
#define ZTEST_SUITE(...)

#define zassert_equal(actual, expected, ...)                                      \
	do {                                                                         \
		if (!host_assert_equal((long long)(actual), (long long)(expected),     \
				       #actual " == " #expected, __FILE__, __LINE__)) { \
			return;                                                        \
		}                                                                    \
	} while (0)

#define zassert_true(actual, ...)                                                 \
	do {                                                                         \
		if (!host_assert_true(!!(actual), #actual, __FILE__, __LINE__)) {     \
			return;                                                        \
		}                                                                    \
	} while (0)

#define zassert_false(actual, ...)                                                \
	do {                                                                         \
		if (!host_assert_false(!!(actual), #actual, __FILE__, __LINE__)) {    \
			return;                                                        \
		}                                                                    \
	} while (0)

#endif /* POWER_LOGIC_HOST_ZEPHYR_ZTEST_H_ */
