/*
 * JamShield - sensor thread (LDR or DHT11).
 *
 * Reads a sensor every period on a dedicated thread and exposes the latest
 * sample thread-safely. Two sensors are supported on separate pins; the build
 * switch CONFIG_JS_SENSOR_DHT11 chooses which one is read:
 *   DHT11 (default) : temperature + humidity, digital 1-wire on GPIO4.
 *   LDR             : light, analog ADC on GPIO34.
 * Integer math only (no float in any thread; Xtensa LX6 FP rule).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "sensor_ldr.h"
#include "jam_detect.h"

LOG_MODULE_REGISTER(sensor_ldr, LOG_LEVEL_INF);

#define SENSOR_STACK_SIZE 1024
#define SENSOR_PRIORITY   5

#if defined(CONFIG_JS_SENSOR_DHT11)
#include <zephyr/drivers/sensor.h>
#define SENSOR_PERIOD_MS 2000                      /* DHT11 max ~1 sample/s */
static const struct device *const dht = DEVICE_DT_GET_ONE(aosong_dht);
#else
#include <zephyr/drivers/adc.h>
#define SENSOR_PERIOD_MS 1000
static const struct adc_dt_spec adc_ldr = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

static struct ldr_reading latest;
static K_MUTEX_DEFINE(latest_lock);

static K_THREAD_STACK_DEFINE(sensor_stack, SENSOR_STACK_SIZE);
static struct k_thread sensor_thread_data;

uint32_t sensor_ldr_to_lux_x10(uint16_t adc_raw)
{
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

static void store(uint16_t adc_raw, int16_t temp_c10, uint8_t humidity)
{
	uint32_t now = (uint32_t)k_uptime_get();

	k_mutex_lock(&latest_lock, K_FOREVER);
	latest.adc_raw = adc_raw;
	latest.temp_c10 = temp_c10;
	latest.humidity = humidity;
	latest.timestamp_ms = now;
	k_mutex_unlock(&latest_lock);

	/* Feed the adaptive jamming thresholds (LDR value; harmless for DHT11). */
	update_thresholds_from_ldr(adc_raw);
}

#if defined(CONFIG_JS_SENSOR_DHT11)
static int read_once(void)
{
	struct sensor_value t, h;
	int ret = sensor_sample_fetch(dht);

	if (ret != 0) {
		return ret;                        /* DHT11 read failed this cycle */
	}
	sensor_channel_get(dht, SENSOR_CHAN_AMBIENT_TEMP, &t);
	sensor_channel_get(dht, SENSOR_CHAN_HUMIDITY, &h);

	int16_t tc10 = (int16_t)(t.val1 * 10 + t.val2 / 100000);
	uint8_t hum = (uint8_t)(h.val1 < 0 ? 0 : (h.val1 > 100 ? 100 : h.val1));

	/* Put temperature (whole C) into adc_raw so the existing "sensor value"
	 * fields keep working; temp_c10/humidity carry the precise readings. */
	store((uint16_t)(t.val1 < 0 ? 0 : t.val1), tc10, hum);
	LOG_INF("DHT11: %d.%dC  %u%%RH", tc10 / 10, tc10 % 10, hum);
	return 0;
}
#else
static int read_once(void)
{
	int16_t sample = 0;
	struct adc_sequence seq = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};

	(void)adc_sequence_init_dt(&adc_ldr, &seq);
	seq.resolution = adc_ldr.resolution;

	int ret = adc_read_dt(&adc_ldr, &seq);

	if (ret != 0) {
		return ret;
	}
	if (sample < 0) {
		sample = 0;
	}
	uint16_t adc_raw = (uint16_t)sample;
	uint32_t lux_x10 = sensor_ldr_to_lux_x10(adc_raw);

	store(adc_raw, 0, 0);
	LOG_INF("LDR: adc=%u, lux=%u.%u", adc_raw, lux_x10 / 10U, lux_x10 % 10U);
	return 0;
}
#endif

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		int ret = read_once();

		if (ret != 0) {
			LOG_WRN("sensor read failed: %d (keeping last value)", ret);
		}
		k_msleep(SENSOR_PERIOD_MS);
	}
}

int sensor_ldr_init(void)
{
#if defined(CONFIG_JS_SENSOR_DHT11)
	if (!device_is_ready(dht)) {
		LOG_ERR("DHT11 device not ready");
		return -ENODEV;
	}
	LOG_INF("DHT11 sensor ready (temperature + humidity)");
#else
	if (!adc_is_ready_dt(&adc_ldr)) {
		LOG_ERR("ADC device %s not ready", adc_ldr.dev->name);
		return -ENODEV;
	}
	int ret = adc_channel_setup_dt(&adc_ldr);

	if (ret != 0) {
		LOG_ERR("ADC channel setup failed: %d", ret);
		return ret;
	}
	LOG_INF("LDR sensor initialized on %s ch%u", adc_ldr.dev->name,
		adc_ldr.channel_id);
#endif

	k_thread_create(&sensor_thread_data, sensor_stack,
			K_THREAD_STACK_SIZEOF(sensor_stack),
			sensor_thread_fn, NULL, NULL, NULL,
			SENSOR_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&sensor_thread_data, "sensor");
	return 0;
}
