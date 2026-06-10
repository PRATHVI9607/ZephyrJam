/*
 * JamShield — bearer manager / protocol-hopping FSM (PRD.md Sections 6.3, 9).
 *
 * Unifies the three bearers behind one "active bearer" and one send() entry
 * point, and drives failover/restore from jamming-detection state changes:
 *
 *   WiFi (prio 1) --jam confirmed--> BLE (prio 2) --BLE down--> ESP-NOW (prio 3)
 *        ^------------------------ jam cleared --------------------------|
 *
 * WiFi association is intentionally kept up during failover so the detector can
 * keep sampling RSSI and spot jam cessation; we simply stop publishing on it.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "conn_mgr_setup.h"
#include "wifi_mqtt.h"
#include "ble_gatt.h"
#include "espnow_l2.h"

LOG_MODULE_REGISTER(conn_mgr, LOG_LEVEL_INF);

static volatile enum js_channel active = JS_CH_WIFI;
static uint64_t last_switch_ms;

const char *js_channel_str(enum js_channel ch)
{
	switch (ch) {
	case JS_CH_WIFI:   return "WIFI";
	case JS_CH_BLE:    return "BLE";
	case JS_CH_ESPNOW: return "ESPNOW";
	default:           return "UNKNOWN";
	}
}

enum js_channel conn_mgr_active_bearer(void)
{
	return active;
}

static void switch_to(enum js_channel to, const char *why)
{
	enum js_channel from = active;
	uint64_t now = k_uptime_get();

	if (to == from) {
		return;
	}

	active = to;
	LOG_WRN("FAILOVER %s -> %s (%s) at t=%llu ms (dwell %llu ms)",
		js_channel_str(from), js_channel_str(to), why, now,
		last_switch_ms ? now - last_switch_ms : 0);
	last_switch_ms = now;

	/* Emit a machine-parseable marker on the serial log for the analysis
	 * pipeline; the RPi4 also infers failover from the channel field.
	 */
	LOG_INF("{\"event\":\"FAILOVER\",\"from\":\"%s\",\"to\":\"%s\",\"ts_ms\":%llu}",
		js_channel_str(from), js_channel_str(to), now);
}

void trigger_bearer_failover(void)
{
	/* Pick the highest-priority bearer below WiFi that is currently usable. */
	if (ble_gatt_is_connected()) {
		switch_to(JS_CH_BLE, "jam confirmed, BLE up");
	} else if (espnow_l2_is_ready()) {
		switch_to(JS_CH_ESPNOW, "jam confirmed, ESP-NOW up");
	} else {
		/* No fallback connected yet: prefer BLE optimistically so the
		 * payload thread starts notifying as soon as a central attaches.
		 */
		switch_to(JS_CH_BLE, "jam confirmed, awaiting BLE central");
	}
}

void attempt_wifi_restore(void)
{
	if (wifi_mqtt_is_connected()) {
		switch_to(JS_CH_WIFI, "jam cleared, WiFi restored");
	}
}

/* Demote one tier when the current fallback also fails. */
static void demote_if_needed(void)
{
	if (active == JS_CH_BLE && !ble_gatt_is_connected() &&
	    espnow_l2_is_ready()) {
		switch_to(JS_CH_ESPNOW, "BLE lost");
	}
}

/* jam_detect state-change callback (runs in detection thread context). */
static void on_jam_state(jam_state_t s)
{
	switch (s) {
	case JAM_STATE_CONFIRMED:
		trigger_bearer_failover();
		break;
	case JAM_STATE_CLEAR:
		attempt_wifi_restore();
		break;
	default:
		break;
	}
}

int conn_mgr_send(const struct ble_sensor_payload *bin, const char *json,
		  size_t json_len)
{
	demote_if_needed();

	switch (active) {
	case JS_CH_WIFI:
		/* While WiFi is still associating at boot, just skip the send.
		 * Failover is driven exclusively by the jam detector, not by a
		 * not-yet-connected primary bearer.
		 */
		if (!wifi_mqtt_is_connected()) {
			return -ENOTCONN;
		}
		jam_detect_record_sent();
		return wifi_mqtt_publish(JS_MQTT_TOPIC, json, json_len);
	case JS_CH_BLE:
		return ble_gatt_send(bin);
	case JS_CH_ESPNOW:
		return espnow_send_payload(bin);
	default:
		return -EINVAL;
	}
}

int conn_mgr_setup_init(void)
{
	jam_detect_register_callback(on_jam_state);
	active = JS_CH_WIFI;
	last_switch_ms = k_uptime_get();
	LOG_INF("Bearer manager ready (active=%s)", js_channel_str(active));
	return 0;
}
