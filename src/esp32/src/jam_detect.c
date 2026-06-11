/*
 * JamShield — jamming detection engine (PRD.md Section 8).
 *
 * Highest-priority application thread (prio 2). Every 100 ms it samples the
 * WiFi RSSI and a sliding-window packet-loss estimate, then runs a dual-metric
 * hysteretic state machine. Detection requires BOTH degraded RSSI AND elevated
 * loss, sustained for >= confirm_ms (min 3 samples), to avoid false positives.
 *
 * On every state transition the registered callback is invoked so the bearer
 * manager can fail over (on CONFIRMED) or restore (on RECOVERING/CLEAR).
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include "jam_detect.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(jam_detect, LOG_LEVEL_INF);

#define JAM_STACK_SIZE        2048
#define JAM_PRIORITY          2
#define SAMPLE_PERIOD_MS      100
#define LOSS_WINDOW           10
#define MIN_CONFIRM_SAMPLES   3

static K_THREAD_STACK_DEFINE(jam_stack, JAM_STACK_SIZE);
static struct k_thread jam_thread_data;

/* Packet accounting (thread-safe counters). */
static atomic_t sent_count;
static atomic_t acked_count;

/* Sliding-window history of cumulative totals. */
static uint32_t sent_hist[LOSS_WINDOW];
static uint32_t acked_hist[LOSS_WINDOW];
static uint8_t hist_idx;
static bool hist_full;

/* Published metrics (single writer = this thread). */
static volatile jam_state_t cur_state = JAM_STATE_CLEAR;
static volatile int8_t cur_rssi;
static volatile uint8_t cur_loss_pct;

static void (*state_cb)(jam_state_t);

void jam_detect_record_sent(void)
{
	atomic_inc(&sent_count);
}

void jam_detect_record_acked(void)
{
	atomic_inc(&acked_count);
}

void jam_detect_register_callback(void (*cb)(jam_state_t new_state))
{
	state_cb = cb;
}

jam_state_t jam_detect_get_state(void)
{
	return cur_state;
}

int8_t jam_detect_get_rssi(void)
{
	return cur_rssi;
}

uint8_t jam_detect_get_loss_pct(void)
{
	return cur_loss_pct;
}

/* Sliding-window loss estimate over the last LOSS_WINDOW samples. */
static uint8_t compute_loss_percentage(void)
{
	uint32_t cur_sent = (uint32_t)atomic_get(&sent_count);
	uint32_t cur_acked = (uint32_t)atomic_get(&acked_count);

	uint32_t old_sent = sent_hist[hist_idx];
	uint32_t old_acked = acked_hist[hist_idx];

	sent_hist[hist_idx] = cur_sent;
	acked_hist[hist_idx] = cur_acked;
	hist_idx = (hist_idx + 1U) % LOSS_WINDOW;
	if (hist_idx == 0U) {
		hist_full = true;
	}

	if (!hist_full) {
		return 0; /* not enough data yet */
	}

	uint32_t d_sent = cur_sent - old_sent;
	uint32_t d_acked = cur_acked - old_acked;

	if (d_sent == 0U) {
		return 0; /* nothing sent in window -> cannot infer loss */
	}
	if (d_acked > d_sent) {
		d_acked = d_sent;
	}
	return (uint8_t)(((d_sent - d_acked) * 100U) / d_sent);
}

static void set_state(jam_state_t s)
{
	cur_state = s;
	if (state_cb) {
		state_cb(s);
	}
}

static void jam_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t consecutive = 0;
	uint64_t suspected_at = 0;

	LOG_INF("Jamming detection thread started (prio %d, %dms)",
		JAM_PRIORITY, SAMPLE_PERIOD_MS);

	while (1) {
		int8_t rssi = wifi_mqtt_get_rssi();
		uint8_t loss = compute_loss_percentage();
		bool degraded = wifi_mqtt_force_jam() ||
				((rssi != 0) &&
				 (rssi < js_thresholds.rssi_threshold) &&
				 (loss > js_thresholds.loss_threshold));

		cur_rssi = rssi;
		cur_loss_pct = loss;

		switch (cur_state) {
		case JAM_STATE_CLEAR:
			if (degraded) {
				consecutive = 1;
				suspected_at = k_uptime_get();
				set_state(JAM_STATE_SUSPECTED);
				LOG_WRN("Jamming SUSPECTED: RSSI=%d loss=%u%%",
					rssi, loss);
			}
			break;

		case JAM_STATE_SUSPECTED: {
			uint32_t need = js_thresholds.confirm_ms / SAMPLE_PERIOD_MS;

			if (need < MIN_CONFIRM_SAMPLES) {
				need = MIN_CONFIRM_SAMPLES;
			}
			if (degraded) {
				consecutive++;
				if (consecutive >= need) {
					uint64_t lat = k_uptime_get() - suspected_at;

					set_state(JAM_STATE_CONFIRMED);
					LOG_ERR("Jamming CONFIRMED! detection latency=%llu ms",
						lat);
				}
			} else {
				consecutive = 0;
				set_state(JAM_STATE_CLEAR);
				LOG_INF("Jamming false alarm cleared");
			}
			break;
		}

		case JAM_STATE_CONFIRMED:
			/* Watch for cessation: RSSI recovering and loss low (and no
			 * manual force-jam still held).
			 */
			if (!wifi_mqtt_force_jam() && rssi != 0 &&
			    rssi > (js_thresholds.rssi_threshold - 10) &&
			    loss < 5U) {
				set_state(JAM_STATE_RECOVERING);
				LOG_INF("Jamming CEASING, attempting restore");
			}
			break;

		case JAM_STATE_RECOVERING:
			if (wifi_mqtt_is_connected() && rssi != 0 &&
			    rssi >= js_thresholds.rssi_threshold) {
				consecutive = 0;
				set_state(JAM_STATE_CLEAR);
				LOG_INF("WiFi RESTORED");
			}
			break;
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}
}

int jam_detect_init(void)
{
	k_thread_create(&jam_thread_data, jam_stack,
			K_THREAD_STACK_SIZEOF(jam_stack),
			jam_thread_fn, NULL, NULL, NULL,
			JAM_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&jam_thread_data, "jam_detect");
	return 0;
}
