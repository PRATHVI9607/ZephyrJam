/*
 * JamShield — LDR sensor thread.
 *
 * Reads the LDR voltage divider on GPIO34 (ADC1_CH6) every 1000 ms using the
 * Zephyr ADC DT API, stores the latest sample under a mutex, and feeds the
 * value into the adaptive jamming thresholds. Integer math only.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include "sensor_ldr.h"
#include "jam_detect.h"

LOG_MODULE_REGISTER(sensor_ldr, LOG_LEVEL_INF);

#define SENSOR_STACK_SIZE 1024
#define SENSOR_PRIORITY   5
#define SENSOR_PERIOD_MS  1000

/* ADC channel comes from the "zephyr,user" node's io-channels (see overlay). */
static const struct adc_dt_spec adc_ldr =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static struct ldr_reading latest;
static K_MUTEX_DEFINE(latest_lock);

static K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_STACK_SIZE);
static struct k_thread sensor_thread_data;

uint32_t sensor_ldr_to_lux_x10(uint16_t adc_raw)
{
	/* Approximate, monotonic with brightness. Darker (LDR high R) -> lower
	 * divider voltage -> higher ADC here is board-dependent; we keep the
	 * PRD formula: brighter light -> lower adc_raw -> higher lux.
	 */
	if (adc_raw > 4095U) {
		adc_raw = 4095U;
	}
	return (uint32_t)(4095U - adc_raw) * 10U / 40U;
}

struct ldr_reading sensor_ldr_get(void)
{
	struct ldr_reading copy;

	k_mutex_lock(&latest_lock, K_FOREVER);
	copy = latest;
	k_mutex_unlock(&latest_lock);
	return copy;
}

static int read_once(uint16_t *out)
{
	int16_t sample = 0; /* 12-bit result fits; signed per ADC API */
	struct adc_sequence seq = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};
	int ret;

	(void)adc_sequence_init_dt(&adc_ldr, &seq);
	seq.resolution = adc_ldr.resolution;

	ret = adc_read_dt(&adc_ldr, &seq);
	if (ret != 0) {
		return ret;
	}

	if (sample < 0) {
		sample = 0;
	}
	*out = (uint16_t)sample;
	return 0;
}

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		uint16_t adc_raw = 0;
		int ret = read_once(&adc_raw);

		if (ret == 0) {
			uint32_t now = (uint32_t)k_uptime_get();

			k_mutex_lock(&latest_lock, K_FOREVER);
			latest.adc_raw = adc_raw;
			latest.timestamp_ms = now;
			k_mutex_unlock(&latest_lock);

			/* Feed the adaptive jamming thresholds (PRD 6.5). */
			update_thresholds_from_ldr(adc_raw);

			uint32_t lux_x10 = sensor_ldr_to_lux_x10(adc_raw);

			LOG_INF("LDR: adc=%u, lux=%u.%u", adc_raw,
				lux_x10 / 10U, lux_x10 % 10U);
		} else {
			LOG_ERR("ADC read failed: %d", ret);
		}

		k_msleep(SENSOR_PERIOD_MS);
	}
}

int sensor_ldr_init(void)
{
	int ret;

	if (!adc_is_ready_dt(&adc_ldr)) {
		LOG_ERR("ADC device %s not ready", adc_ldr.dev->name);
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&adc_ldr);
	if (ret != 0) {
		LOG_ERR("ADC channel setup failed: %d", ret);
		return ret;
	}

	k_thread_create(&sensor_thread_data, sensor_stack,
			K_THREAD_STACK_SIZEOF(sensor_stack),
			sensor_thread_fn, NULL, NULL, NULL,
			SENSOR_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&sensor_thread_data, "sensor");

	LOG_INF("LDR sensor initialized on %s ch%u",
		adc_ldr.dev->name, adc_ldr.channel_id);
	return 0;
}
