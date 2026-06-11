/*
 * JamShield — shared definitions
 *
 * Common types, the wire payload struct, and project-wide constants used
 * across every module. Keep this header free of Zephyr-subsystem includes so
 * it can be pulled in by any translation unit cheaply.
 */
#ifndef JAMSHIELD_H
#define JAMSHIELD_H

#include <stdint.h>
#include <zephyr/toolchain.h> /* __packed */

/* ---- Active communication bearer ---------------------------------------- */
enum js_channel {
	JS_CH_WIFI   = 0,
	JS_CH_BLE    = 1,
	JS_CH_ESPNOW = 2,
};

/* Human-readable channel name (always valid, never NULL). */
const char *js_channel_str(enum js_channel ch);

/* ---- Compact binary payload (BLE + ESP-NOW), 18 bytes ------------------- *
 * Layout is fixed and shared with the RPi4 receiver (struct.unpack '<IQBHbBB').
 * Do NOT reorder fields — the Python side depends on this exact layout.
 */
struct ble_sensor_payload {
	uint32_t seq;       /* bytes 0-3   : monotonic sequence number       */
	uint64_t ts_ms;     /* bytes 4-11  : k_uptime_get() at build time    */
	uint8_t  channel;   /* byte  12    : enum js_channel                 */
	uint16_t ldr_adc;   /* bytes 13-14 : raw 12-bit ADC reading          */
	int8_t   rssi;      /* byte  15    : last WiFi RSSI (dBm)            */
	uint8_t  jam_state; /* byte  16    : enum jam_state                  */
	uint8_t  cpu_util;  /* byte  17    : 0-100 %                          */
} __packed;

BUILD_ASSERT(sizeof(struct ble_sensor_payload) == 18,
	     "ble_sensor_payload must be exactly 18 bytes on the wire");

/* ---- Project-wide configuration ----------------------------------------- */
#define JS_WIFI_SSID        "Loki"
#define JS_WIFI_PSK         "loki2536"
#define JS_MQTT_BROKER_IP   "10.88.34.137"  /* RPi4 on the LAN (broker) */
#define JS_MQTT_BROKER_PORT 1883
#define JS_MQTT_TOPIC       "jamshield/sensor/ldr"
#define JS_MQTT_EVENT_TOPIC "jamshield/events/failover"
#define JS_BT_DEVICE_NAME   "JamShield"

#endif /* JAMSHIELD_H */
