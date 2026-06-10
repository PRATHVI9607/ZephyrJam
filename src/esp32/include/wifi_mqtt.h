/*
 * JamShield — WiFi station + MQTT publisher (primary bearer).
 *
 * Connects to the RPi4 hostapd network and publishes JSON sensor payloads to
 * the broker. Exposes connection state and the last measured RSSI for the
 * jamming detector. See PRD.md Section 7.1.
 */
#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Kick off WiFi association + MQTT connect (non-blocking; runs a mgmt thread).
 * Returns 0 if the connect sequence was started successfully.
 */
int wifi_mqtt_connect(void);

/* Tear down the MQTT session and disconnect WiFi (used on failover). */
void wifi_mqtt_disconnect(void);

/* Publish a pre-formatted payload to JS_MQTT_TOPIC at QoS1. */
int wifi_mqtt_publish(const char *topic, const char *payload, size_t len);

/* True once both WiFi (L4) and MQTT are connected. */
bool wifi_mqtt_is_connected(void);

/* Last RSSI sampled from the WiFi interface (dBm); 0 if unknown. */
int8_t wifi_mqtt_get_rssi(void);

/* Must be pumped regularly to service MQTT keepalive / RX (called by payload thread). */
void wifi_mqtt_process(void);

#endif /* WIFI_MQTT_H */
