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

LOG_MODULE_REGISTER(bearer_mgr, LOG_LEVEL_INF);

static volatile enum js_channel active = JS_CH_WIFI;
static volatile enum js_mode mode = JS_MODE_HOP;
static uint64_t last_switch_ms;

const char *js_mode_str(enum js_mode m)
{
	switch (m) {
	case JS_MODE_HOP:   return "HOP";
	case JS_MODE_NOHOP: return "NOHOP";
	case JS_MODE_NOBLE: return "NOBLE";
	default:            return "?";
	}
}

enum js_mode js_mode_get(void)
{
	return mode;
}

void js_mode_set(enum js_mode m)
{
	mode = m;
	LOG_WRN("MODE set to %s", js_mode_str(m));
	/* Returning to HOP/normal: if no jam, make sure we're back on WiFi. */
	if (m != JS_MODE_NOHOP) {
		attempt_wifi_restore();
	}
}

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
	if (mode == JS_MODE_NOHOP) {
		/* Protection OFF: demonstrate the problem — stay on WiFi and let
		 * packets drop while jammed. No failover.
		 */
		LOG_WRN("Jam detected, mode=NOHOP: staying on WiFi, packets DROPPING");
		return;
	}

	if (mode == JS_MODE_NOBLE) {
		/* Skip BLE entirely: WiFi -> ESP-NOW. */
		switch_to(JS_CH_ESPNOW, "jam confirmed, NOBLE -> ESP-NOW");
		return;
	}

	/* HOP: highest-priority bearer below WiFi that is currently usable. */
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
	if (mode == JS_MODE_HOP && active == JS_CH_BLE &&
	    !ble_gatt_is_connected() && espnow_l2_is_ready()) {
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
		/* WiFi can't deliver if it's down OR jammed. In NO-HOP mode the
		 * bearer stays WiFi, so this makes packets visibly drop while
		 * jammed (the "unprotected" demo). In HOP/NO-BLE we've already
		 * switched bearers, so this path isn't taken once failed over.
		 */
		if (!wifi_mqtt_is_connected() || wifi_mqtt_force_jam()) {
			return -ENOTCONN;
		}
		jam_detect_record_sent();
		return wifi_mqtt_publish(JS_MQTT_TOPIC, json, json_len);
	case JS_CH_BLE:
		return ble_gatt_send(bin);
	case JS_CH_ESPNOW:
		(void)espnow_send_payload(bin);   /* real ESP-NOW frame over the air */
		/* No OTA ESP-NOW receiver in this setup -> mirror the same telemetry
		 * over MQTT so the Pi/app can show the ESP-NOW bearer (logical jam
		 * keeps WiFi up). The JSON's channel field already reads "ESPNOW". */
		if (wifi_mqtt_is_connected()) {
			return wifi_mqtt_publish(JS_ESPNOW_TOPIC, json, json_len);
		}
		return 0;
	default:
		return -EINVAL;
	}
}

int conn_mgr_setup_init(void)
{
	jam_detect_register_callback(on_jam_state);
	active = JS_CH_WIFI;
	last_switch_ms = k_uptime_get();
	LOG_INF("Bearer manager ready (active=%s mode=%s)",
		js_channel_str(active), js_mode_str(mode));
	return 0;
}
