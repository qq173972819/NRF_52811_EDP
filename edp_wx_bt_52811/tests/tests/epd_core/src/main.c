/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#ifdef HOST_TEST
#include "epd/epd_154d.h"
#include "../host/adapter_shim.h"
#endif
#include "epd/epd_154d_core.h"

enum event_kind {
	EVENT_RESET,
	EVENT_COMMAND,
	EVENT_DATA,
	EVENT_WAIT,
	EVENT_DELAY,
};

struct event {
	enum event_kind kind;
	int32_t value;
	size_t length;
	uint8_t data[8];
};

#define MAX_EVENTS 64U
#define INJECTED_ERROR (-73)

struct fake_bus {
	struct event events[MAX_EVENTS];
	size_t event_count;
	size_t fail_at;
};

static struct fake_bus fake;
static struct epd_154d_core core;
static uint8_t image[EPD_154D_IMAGE_BYTES];

static int record_event(enum event_kind kind, int32_t value, const uint8_t *data,
			size_t length, void *context)
{
	struct fake_bus *bus = context;
	struct event *event;

	if (bus->event_count >= MAX_EVENTS) {
		return -ENOMEM;
	}

	event = &bus->events[bus->event_count++];
	event->kind = kind;
	event->value = value;
	event->length = length;
	if (data != NULL) {
		memcpy(event->data, data, length < sizeof(event->data) ? length :
		       sizeof(event->data));
	}

	return bus->fail_at == bus->event_count ? INJECTED_ERROR : 0;
}

static int fake_reset(bool asserted, void *context)
{
	return record_event(EVENT_RESET, asserted, NULL, 0, context);
}

static int fake_command(uint8_t command, void *context)
{
	return record_event(EVENT_COMMAND, command, NULL, 0, context);
}

static int fake_data(const uint8_t *data, size_t length, void *context)
{
	return record_event(EVENT_DATA, 0, data, length, context);
}

static int fake_wait(int32_t timeout_ms, void *context)
{
	return record_event(EVENT_WAIT, timeout_ms, NULL, 0, context);
}

static void fake_delay(int32_t delay_ms, void *context)
{
	(void)record_event(EVENT_DELAY, delay_ms, NULL, 0, context);
}

static const struct epd_154d_bus bus = {
	.reset = fake_reset,
	.write_command = fake_command,
	.write_data = fake_data,
	.wait_ready = fake_wait,
	.delay_ms = fake_delay,
	.context = &fake,
};

static void reset_fixture(void)
{
	memset(&fake, 0, sizeof(fake));
	memset(&core, 0, sizeof(core));
}

static void assert_event(size_t index, enum event_kind kind, int32_t value,
			 const uint8_t *data, size_t length)
{
	zassert_true(index < fake.event_count);
	zassert_equal(fake.events[index].kind, kind);
	zassert_equal(fake.events[index].value, value);
	zassert_equal(fake.events[index].length, length);
	const size_t recorded_length = length < sizeof(fake.events[index].data) ?
				       length : sizeof(fake.events[index].data);

	for (size_t i = 0; i < recorded_length; ++i) {
		zassert_equal(fake.events[index].data[i], data[i]);
	}
}

static void init_successfully(void)
{
	reset_fixture();
	zassert_equal(epd_154d_core_init(&core, &bus), 0);
	fake.event_count = 0;
}

ZTEST(epd_core_init, test_exact_sequence)
{
	static const uint8_t d4d[] = { 0x78 };
	static const uint8_t d00[] = { 0x0f, 0x29 };
	static const uint8_t d06[] = { 0x0d, 0x12, 0x30, 0x20, 0x19, 0x2a, 0x22 };
	static const uint8_t d50[] = { 0x37 };
	static const uint8_t d61[] = { 0x00, 0xc8, 0x00, 0xc8 };
	static const uint8_t de9[] = { 0x01 };
	static const uint8_t d30[] = { 0x08 };

	reset_fixture();
	zassert_equal(epd_154d_core_init(&core, &bus), 0);
	zassert_equal(fake.event_count, 21);
	assert_event(0, EVENT_RESET, 1, NULL, 0);
	assert_event(1, EVENT_DELAY, 50, NULL, 0);
	assert_event(2, EVENT_RESET, 0, NULL, 0);
	assert_event(3, EVENT_DELAY, 50, NULL, 0);
	assert_event(4, EVENT_WAIT, EPD_BUSY_TIMEOUT_MS, NULL, 0);
	assert_event(5, EVENT_COMMAND, 0x4d, NULL, 0);
	assert_event(6, EVENT_DATA, 0, d4d, sizeof(d4d));
	assert_event(7, EVENT_COMMAND, 0x00, NULL, 0);
	assert_event(8, EVENT_DATA, 0, d00, sizeof(d00));
	assert_event(9, EVENT_COMMAND, 0x06, NULL, 0);
	assert_event(10, EVENT_DATA, 0, d06, sizeof(d06));
	assert_event(11, EVENT_COMMAND, 0x50, NULL, 0);
	assert_event(12, EVENT_DATA, 0, d50, sizeof(d50));
	assert_event(13, EVENT_COMMAND, 0x61, NULL, 0);
	assert_event(14, EVENT_DATA, 0, d61, sizeof(d61));
	assert_event(15, EVENT_COMMAND, 0xe9, NULL, 0);
	assert_event(16, EVENT_DATA, 0, de9, sizeof(de9));
	assert_event(17, EVENT_COMMAND, 0x30, NULL, 0);
	assert_event(18, EVENT_DATA, 0, d30, sizeof(d30));
	assert_event(19, EVENT_COMMAND, 0x04, NULL, 0);
	assert_event(20, EVENT_WAIT, EPD_BUSY_TIMEOUT_MS, NULL, 0);
}

ZTEST(epd_core_init, test_rejects_invalid_arguments_without_bus_access)
{
	struct epd_154d_bus incomplete_bus = bus;

	reset_fixture();
	zassert_equal(epd_154d_core_init(NULL, &bus), -EINVAL);
	zassert_equal(epd_154d_core_init(&core, NULL), -EINVAL);
	incomplete_bus.write_data = NULL;
	zassert_equal(epd_154d_core_init(&core, &incomplete_bus), -EINVAL);
	zassert_equal(fake.event_count, 0);
}

ZTEST(epd_core_init, test_propagates_each_injected_bus_error_and_stops)
{
	static const size_t fallible_events[] = {
		1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
		19, 20, 21,
	};

	for (size_t i = 0; i < sizeof(fallible_events) / sizeof(fallible_events[0]);
	     ++i) {
		reset_fixture();
		fake.fail_at = fallible_events[i];
		zassert_equal(epd_154d_core_init(&core, &bus), INJECTED_ERROR);
		zassert_equal(fake.event_count, fallible_events[i]);
		zassert_equal(epd_154d_core_begin_image(&core), -EPIPE);
	}
}

ZTEST(epd_core_init, test_powered_state_can_sleep_after_final_wait_failure)
{
	static const uint8_t zero[] = { 0x00 };
	static const uint8_t key[] = { 0xa5 };

	reset_fixture();
	fake.fail_at = 20;
	zassert_equal(epd_154d_core_init(&core, &bus), INJECTED_ERROR);
	fake.fail_at = 0;
	zassert_equal(epd_154d_core_sleep(&core), -EPIPE);
	zassert_equal(fake.event_count, 20);

	reset_fixture();
	fake.fail_at = 21;
	zassert_equal(epd_154d_core_init(&core, &bus), INJECTED_ERROR);
	fake.fail_at = 0;
	fake.event_count = 0;
	zassert_equal(epd_154d_core_sleep(&core), 0);
	zassert_equal(fake.event_count, 5);
	assert_event(0, EVENT_COMMAND, 0x02, NULL, 0);
	assert_event(1, EVENT_DATA, 0, zero, sizeof(zero));
	assert_event(2, EVENT_WAIT, EPD_BUSY_TIMEOUT_MS, NULL, 0);
	assert_event(3, EVENT_COMMAND, 0x07, NULL, 0);
	assert_event(4, EVENT_DATA, 0, key, sizeof(key));
}

ZTEST(epd_core_stream, test_rejects_calls_before_initialization)
{
	reset_fixture();
	zassert_equal(epd_154d_core_begin_image(&core), -EPIPE);
	zassert_equal(epd_154d_core_write_image(&core, image, 1), -EPIPE);
	zassert_equal(epd_154d_core_refresh(&core), -EPIPE);
	zassert_equal(epd_154d_core_sleep(&core), -EPIPE);
	zassert_equal(fake.event_count, 0);
}

ZTEST(epd_core_stream, test_begin_and_chunked_write_track_exact_bytes)
{
	init_successfully();
	zassert_equal(epd_154d_core_begin_image(&core), 0);
	zassert_equal(core.bytes_written, 0);
	assert_event(0, EVENT_COMMAND, 0x10, NULL, 0);

	zassert_equal(epd_154d_core_write_image(&core, image, 37), 0);
	zassert_equal(epd_154d_core_write_image(&core, image + 37,
					      EPD_154D_IMAGE_BYTES - 37), 0);
	zassert_equal(core.bytes_written, EPD_154D_IMAGE_BYTES);
	zassert_equal(fake.event_count, 3);
	assert_event(1, EVENT_DATA, 0, image, 37);
	zassert_equal(fake.events[2].kind, EVENT_DATA);
	zassert_equal(fake.events[2].length, EPD_154D_IMAGE_BYTES - 37);
}

ZTEST(epd_core_stream, test_rejects_invalid_and_overflow_writes_without_mutation)
{
	init_successfully();
	zassert_equal(epd_154d_core_begin_image(&core), 0);
	fake.event_count = 0;
	zassert_equal(epd_154d_core_write_image(&core, NULL, 1), -EINVAL);
	zassert_equal(epd_154d_core_write_image(&core, image, 0), -EINVAL);
	zassert_equal(epd_154d_core_write_image(NULL, image, 1), -EINVAL);
	zassert_equal(core.bytes_written, 0);
	zassert_equal(fake.event_count, 0);

	zassert_equal(epd_154d_core_write_image(&core, image,
					      EPD_154D_IMAGE_BYTES), 0);
	fake.event_count = 0;
	zassert_equal(epd_154d_core_write_image(&core, image, 1), -EMSGSIZE);
	zassert_equal(core.bytes_written, EPD_154D_IMAGE_BYTES);
	zassert_equal(fake.event_count, 0);
}

ZTEST(epd_core_stream, test_failed_begin_and_write_do_not_change_state)
{
	init_successfully();
	fake.fail_at = 1;
	zassert_equal(epd_154d_core_begin_image(&core), INJECTED_ERROR);
	zassert_equal(core.bytes_written, 0);
	zassert_equal(fake.event_count, 1);

	fake.fail_at = 0;
	fake.event_count = 0;
	zassert_equal(epd_154d_core_begin_image(&core), 0);
	fake.event_count = 0;
	fake.fail_at = 1;
	zassert_equal(epd_154d_core_write_image(&core, image, 16), INJECTED_ERROR);
	zassert_equal(core.bytes_written, 0);
	zassert_equal(fake.event_count, 1);
}

ZTEST(epd_core_stream, test_rejects_out_of_order_calls_without_bus_access)
{
	init_successfully();
	zassert_equal(epd_154d_core_write_image(&core, image, 1), -EPIPE);
	zassert_equal(epd_154d_core_refresh(&core), -EPIPE);
	zassert_equal(fake.event_count, 0);

	zassert_equal(epd_154d_core_begin_image(&core), 0);
	zassert_equal(epd_154d_core_begin_image(&core), -EPIPE);
	zassert_equal(fake.event_count, 1);
	zassert_equal(epd_154d_core_write_image(&core, image,
					      EPD_154D_IMAGE_BYTES), 0);
	zassert_equal(epd_154d_core_refresh(&core), 0);
	fake.event_count = 0;
	zassert_equal(epd_154d_core_write_image(&core, image, 1), -EPIPE);
	zassert_equal(epd_154d_core_refresh(&core), -EPIPE);
	zassert_equal(fake.event_count, 0);
}

ZTEST(epd_core_refresh, test_requires_exact_image_and_writes_exact_sequence)
{
	static const uint8_t zero[] = { 0x00 };

	init_successfully();
	zassert_equal(epd_154d_core_begin_image(&core), 0);
	zassert_equal(epd_154d_core_write_image(&core, image,
					      EPD_154D_IMAGE_BYTES), 0);
	fake.event_count = 0;
	zassert_equal(epd_154d_core_refresh(&core), 0);
	zassert_equal(fake.event_count, 3);
	assert_event(0, EVENT_COMMAND, 0x12, NULL, 0);
	assert_event(1, EVENT_DATA, 0, zero, sizeof(zero));
	assert_event(2, EVENT_WAIT, EPD_BUSY_TIMEOUT_MS, NULL, 0);
}

ZTEST(epd_core_refresh, test_rejects_incomplete_image_without_bus_access)
{
	init_successfully();
	zassert_equal(epd_154d_core_begin_image(&core), 0);
	zassert_equal(epd_154d_core_write_image(&core, image,
					      EPD_154D_IMAGE_BYTES - 1), 0);
	fake.event_count = 0;
	zassert_equal(epd_154d_core_refresh(&core), -EMSGSIZE);
	zassert_equal(fake.event_count, 0);
}

ZTEST(epd_core_refresh, test_each_bus_error_stops_sequence_and_allows_retry)
{
	for (size_t fail_at = 1; fail_at <= 3; ++fail_at) {
		init_successfully();
		zassert_equal(epd_154d_core_begin_image(&core), 0);
		zassert_equal(epd_154d_core_write_image(&core, image,
						      EPD_154D_IMAGE_BYTES), 0);
		fake.event_count = 0;
		fake.fail_at = fail_at;
		zassert_equal(epd_154d_core_refresh(&core), INJECTED_ERROR);
		zassert_equal(fake.event_count, fail_at);
		fake.fail_at = 0;
		fake.event_count = 0;
		zassert_equal(epd_154d_core_refresh(&core), 0);
	}
}

ZTEST(epd_core_sleep, test_writes_exact_sequence)
{
	static const uint8_t zero[] = { 0x00 };
	static const uint8_t key[] = { 0xa5 };

	init_successfully();
	zassert_equal(epd_154d_core_sleep(&core), 0);
	zassert_equal(fake.event_count, 5);
	assert_event(0, EVENT_COMMAND, 0x02, NULL, 0);
	assert_event(1, EVENT_DATA, 0, zero, sizeof(zero));
	assert_event(2, EVENT_WAIT, EPD_BUSY_TIMEOUT_MS, NULL, 0);
	assert_event(3, EVENT_COMMAND, 0x07, NULL, 0);
	assert_event(4, EVENT_DATA, 0, key, sizeof(key));
}

ZTEST(epd_core_sleep, test_each_bus_error_stops_sequence)
{
	for (size_t fail_at = 1; fail_at <= 5; ++fail_at) {
		init_successfully();
		fake.fail_at = fail_at;
		zassert_equal(epd_154d_core_sleep(&core), INJECTED_ERROR);
		zassert_equal(fake.event_count, fail_at);
	}
}

#ifdef HOST_TEST
ZTEST(epd_adapter, test_all_apis_reject_calls_before_initialization)
{
	zassert_equal(epd_154d_begin_image(), -ENODEV);
	zassert_equal(epd_154d_write_image(image, 1), -ENODEV);
	zassert_equal(epd_154d_refresh(), -ENODEV);
	zassert_equal(epd_154d_abort(), -ENODEV);
}

ZTEST(epd_adapter, test_init_drives_spi_gpio_and_busy_polling)
{
	host_adapter_reset();
	host_adapter.busy_reads_remaining = 2;
	zassert_equal(epd_154d_init(), 0);
	zassert_equal(host_adapter.configure_count, 3);
	zassert_equal(host_adapter.reset_set_count, 2);
	zassert_equal(host_adapter.reset_values[0], 1);
	zassert_equal(host_adapter.reset_values[1], 0);
	zassert_equal(host_adapter.busy_get_count, 4);
	zassert_equal(host_adapter.sleep_count, 4);
	zassert_equal(host_adapter.sleep_total_ms, 110);
	zassert_equal(host_adapter.spi_count, 15);
	zassert_equal(host_adapter.spi_events[0].dc, 0);
	zassert_equal(host_adapter.spi_events[0].length, 1);
	zassert_equal(host_adapter.spi_events[0].first_byte, 0x4d);
	zassert_equal(host_adapter.spi_events[1].dc, 1);
	zassert_equal(host_adapter.spi_events[1].first_byte, 0x78);
	zassert_equal(host_adapter.spi_events[5].length, 7);
	zassert_equal(host_adapter.spi_events[14].dc, 0);
	zassert_equal(host_adapter.spi_events[14].first_byte, 0x04);
	zassert_equal(epd_154d_abort(), 0);
}

ZTEST(epd_adapter, test_init_propagates_busy_read_error_and_timeout)
{
	host_adapter_reset();
	host_adapter.busy_error = -84;
	zassert_equal(epd_154d_init(), -84);
	zassert_equal(epd_154d_begin_image(), -ENODEV);
	host_adapter.busy_error = 0;
	host_adapter_clear_events();
	zassert_equal(epd_154d_abort(), -EPIPE);
	zassert_equal(host_adapter.spi_count, 0);

	host_adapter_reset();
	host_adapter.busy_reads_remaining = 7000;
	zassert_equal(epd_154d_init(), -ETIMEDOUT);
	zassert_equal(host_adapter.busy_get_count, 6001);
	zassert_equal(host_adapter.sleep_count, 6002);
	zassert_equal(host_adapter.sleep_total_ms, 30100);
}

ZTEST(epd_adapter, test_failed_abort_requires_reinitialization)
{
	host_adapter_reset();
	zassert_equal(epd_154d_init(), 0);
	host_adapter_clear_events();
	host_adapter.fail_spi_at = 1;
	zassert_equal(epd_154d_abort(), -EIO);
	zassert_equal(epd_154d_begin_image(), -ENODEV);

	host_adapter_reset();
	zassert_equal(epd_154d_init(), 0);
	zassert_equal(host_adapter.reset_set_count, 2);
	zassert_equal(epd_154d_abort(), 0);
}

ZTEST(epd_adapter, test_abort_recovers_powered_init_failure)
{
	host_adapter_reset();
	host_adapter.busy_error = -84;
	host_adapter.fail_busy_at = 2;
	zassert_equal(epd_154d_init(), -84);
	zassert_equal(host_adapter.spi_count, 15);
	zassert_equal(host_adapter.spi_events[14].first_byte, 0x04);

	host_adapter.busy_error = 0;
	host_adapter.fail_busy_at = 0;
	host_adapter_clear_events();
	zassert_equal(epd_154d_abort(), 0);
	zassert_equal(host_adapter.spi_count, 4);
	zassert_equal(host_adapter.spi_events[0].dc, 0);
	zassert_equal(host_adapter.spi_events[0].first_byte, 0x02);
	zassert_equal(host_adapter.spi_events[1].dc, 1);
	zassert_equal(host_adapter.spi_events[1].first_byte, 0x00);
	zassert_equal(host_adapter.spi_events[2].dc, 0);
	zassert_equal(host_adapter.spi_events[2].first_byte, 0x07);
	zassert_equal(host_adapter.spi_events[3].dc, 1);
	zassert_equal(host_adapter.spi_events[3].first_byte, 0xa5);
	zassert_equal(epd_154d_begin_image(), -ENODEV);

	host_adapter_reset();
	zassert_equal(epd_154d_init(), 0);
	zassert_equal(host_adapter.reset_set_count, 2);
	zassert_equal(epd_154d_abort(), 0);
}
#endif

ZTEST_SUITE(epd_core_init, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(epd_core_stream, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(epd_core_refresh, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(epd_core_sleep, NULL, NULL, NULL, NULL, NULL);
#ifdef HOST_TEST
ZTEST_SUITE(epd_adapter, NULL, NULL, NULL, NULL, NULL);
#endif
