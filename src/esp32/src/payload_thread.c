/*
 * JamShield — payload thread (PRD.md Section 7.2 wiring).
 *
 * Priority 6, 500 ms period. Each cycle it snapshots the sensor + detector
 * state, builds both the JSON (WiFi/MQTT) and the compact 18-byte binary
 * (BLE/ESP-NOW) representations, and hands them to the bearer manager which
 * sends over whichever channel is active. Sequence numbers are monotonic
 * across all channels so the RPi4 can measure per-failover packet loss.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "jamshield.h"
#include "conn_mgr_setup.h"
#include "sensor_ldr.h"
#include "jam_detect.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(payload, LOG_LEVEL_INF);

#define PAYLOAD_STACK_SIZE 2048
#define PAYLOAD_PRIORITY   6
#define PAYLOAD_PERIOD_MS  500

static K_THREAD_STACK_DEFINE(payload_stack, PAYLOAD_STACK_SIZE);
static struct k_thread payload_thread_data;

static atomic_t seq_counter;

static const char *jam_state_str(jam_state_t s)
{
	switch (s) {
	case JAM_STATE_CLEAR:      return "CLEAR";
	case JAM_STATE_SUSPECTED:  return "SUSPECTED";
	case JAM_STATE_CONFIRMED:  return "CONFIRMED";
	case JAM_STATE_RECOVERING: return "RECOVERING";
	default:                   return "UNKNOWN";
	}
}

/* Average CPU utilisation since boot, integer percent (best effort). */
static uint8_t cpu_util_pct(void)
{
#if defined(CONFIG_SCHED_THREAD_USAGE)
	k_thread_runtime_stats_t stats;

	if (k_thread_runtime_stats_all_get(&stats) == 0 &&
	    stats.total_cycles > 0) {
		return (uint8_t)((stats.execution_cycles * 100ULL) /
				 stats.total_cycles);
	}
#endif
	return 0;
}

static void payload_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	char json[256];

	while (1) {
		/* Pump the MQTT socket / keepalive regardless of active bearer. */
		wifi_mqtt_process();

		struct ldr_reading ldr = sensor_ldr_get();
		jam_state_t js = jam_detect_get_state();
		int8_t rssi = jam_detect_get_rssi();
		uint8_t cpu = cpu_util_pct();
		uint32_t seq = (uint32_t)atomic_inc(&seq_counter);
		uint64_t ts = k_uptime_get();
		enum js_channel ch = conn_mgr_active_bearer();
		uint32_t lux_x10 = sensor_ldr_to_lux_x10(ldr.adc_raw);

		struct ble_sensor_payload bin = {
			.seq = seq,
			.ts_ms = ts,
			.channel = (uint8_t)ch,
			.ldr_adc = ldr.adc_raw,
			.rssi = rssi,
			.jam_state = (uint8_t)js,
			.cpu_util = cpu,
		};

		int n = snprintf(json, sizeof(json),
			"{\"seq\":%u,\"ts_ms\":%llu,\"channel\":\"%s\","
			"\"ldr_adc\":%u,\"ldr_lux\":%u.%u,\"rssi\":%d,"
			"\"cpu_util\":%u,\"free_heap\":%u,\"jam_state\":\"%s\"}",
			seq, ts, js_channel_str(ch), ldr.adc_raw,
			lux_x10 / 10U, lux_x10 % 10U, rssi, cpu, 0U,
			jam_state_str(js));

		if (n > 0 && n < (int)sizeof(json)) {
			(void)conn_mgr_send(&bin, json, (size_t)n);
		} else {
			LOG_ERR("payload JSON truncated (%d)", n);
		}

		k_msleep(PAYLOAD_PERIOD_MS);
	}
}

int payload_thread_start(void)
{
	k_thread_create(&payload_thread_data, payload_stack,
			K_THREAD_STACK_SIZEOF(payload_stack),
			payload_thread_fn, NULL, NULL, NULL,
			PAYLOAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&payload_thread_data, "payload");
	return 0;
}
