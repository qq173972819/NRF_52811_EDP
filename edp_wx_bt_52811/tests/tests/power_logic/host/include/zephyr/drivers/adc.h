/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ZEPHYR_DRIVERS_ADC_H_
#define POWER_LOGIC_HOST_ZEPHYR_DRIVERS_ADC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

struct adc_channel_cfg {
	bool differential;
};

struct adc_dt_spec {
	const struct device *dev;
	uint8_t channel_id;
	struct adc_channel_cfg channel_cfg;
	bool channel_cfg_dt_node_exists;
	uint8_t resolution;
	uint8_t oversampling;
	uint32_t node_id;
};

struct adc_sequence {
	uint32_t channels;
	void *buffer;
	size_t buffer_size;
	uint8_t resolution;
	uint8_t oversampling;
};

extern struct device host_adc_device;

#define ADC_DT_SPEC_GET(node_id) ADC_DT_SPEC_GET_RESOLVE(node_id)
#define ADC_DT_SPEC_GET_RESOLVE(node_id) ADC_DT_SPEC_GET_KNOWN(node_id)
#define ADC_DT_SPEC_GET_KNOWN(node_id) ADC_DT_SPEC_GET_##node_id
#define ADC_DT_SPEC_GET_101U                                                    \
	{ &host_adc_device, 0U, { false }, true, 12U, 8U,                         \
	  HOST_DT_PATH_ZEPHYR_USER }

int adc_channel_setup_dt(const struct adc_dt_spec *spec);
int adc_sequence_init_dt(const struct adc_dt_spec *spec,
			 struct adc_sequence *sequence);
int adc_read(const struct device *device, const struct adc_sequence *sequence);
int adc_raw_to_millivolts_dt(const struct adc_dt_spec *spec, int32_t *value);

#endif /* POWER_LOGIC_HOST_ZEPHYR_DRIVERS_ADC_H_ */
