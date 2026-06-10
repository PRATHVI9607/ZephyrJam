/*
 * JamShield — ESP-NOW tertiary bearer.
 *
 * Thin shim over the Espressif HAL exposed by the Zephyr ESP32 port. ESP-NOW
 * is connectionless (no association/DHCP), making it the hardest tier to jam.
 * The same 18-byte ble_sensor_payload is sent as a unicast to the RPi4 MAC.
 * See PRD.md Section 7.3.
 *
 * NOTE: Zephyr has no native ESP-NOW L2. If the Espressif esp_now headers are
 * not available in this build, this module compiles as a logging stub so the
 * rest of the system still builds and runs (guarded by CONFIG_JS_ESPNOW).
 */
#ifndef ESPNOW_L2_H
#define ESPNOW_L2_H

#include "jamshield.h"

/* Initialize ESP-NOW and register the RPi4 as a peer. Returns 0 on success,
 * negative errno on failure, -ENOTSUP if ESP-NOW is not compiled in.
 */
int espnow_l2_init(void);

/* Send one payload as an ESP-NOW unicast. */
int espnow_send_payload(const struct ble_sensor_payload *payload);

/* True if ESP-NOW was initialized and is usable. */
bool espnow_l2_is_ready(void);

#endif /* ESPNOW_L2_H */
