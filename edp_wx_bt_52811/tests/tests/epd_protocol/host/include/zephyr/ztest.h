/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HOST_ZEPHYR_ZTEST_H_
#define HOST_ZEPHYR_ZTEST_H_

int host_assert_equal(long long actual, long long expected, const char *expression,
		      const char *file, int line);
int host_assert_equal_ptr(const void *actual, const void *expected,
			  const char *expression, const char *file, int line);
int host_assert_is_null(const void *actual, const char *expression,
			const char *file, int line);

#define ZTEST(suite, name) void suite##_##name(void)
#define ZTEST_SUITE(...)

#define zassert_equal(actual, expected, ...)                                      \
	do {                                                                         \
		if (!host_assert_equal((long long)(actual), (long long)(expected),     \
				       #actual " == " #expected, __FILE__, __LINE__)) { \
			return;                                                        \
		}                                                                    \
	} while (0)

#define zassert_equal_ptr(actual, expected, ...)                                  \
	do {                                                                         \
		if (!host_assert_equal_ptr((actual), (expected),                     \
					   #actual " == " #expected, __FILE__, __LINE__)) { \
			return;                                                        \
		}                                                                    \
	} while (0)

#define zassert_is_null(actual, ...)                                              \
	do {                                                                         \
		if (!host_assert_is_null((actual), #actual " == NULL", __FILE__,    \
					 __LINE__)) {                                  \
			return;                                                        \
		}                                                                    \
	} while (0)

#endif /* HOST_ZEPHYR_ZTEST_H_ */
