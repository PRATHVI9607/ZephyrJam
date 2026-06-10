/*
 * JamShield — bearer manager / protocol-hopping FSM.
 *
 * Application-level connectivity manager. Zephyr's conn_mgr provides a true
 * bearer abstraction only for net_if-backed L2s (WiFi here); BLE GATT and
 * ESP-NOW are not net interfaces, so this module unifies all three behind a
 * single "active bearer" state machine and a single send() entry point.
 * It also subscribes to WiFi L4 events via conn_mgr/net_mgmt.
 * See PRD.md Sections 6.3 and 9.
 */
#ifndef CONN_MGR_SETUP_H
#define CONN_MGR_SETUP_H

#include "jamshield.h"
#include "jam_detect.h"

/* Initialize the bearer manager: subscribe to WiFi L4 events and arm the
 * jam-detection callback that drives failover. Returns 0 on success.
 */
int conn_mgr_setup_init(void);

/* The currently selected bearer. */
enum js_channel conn_mgr_active_bearer(void);

/* Send one payload over whichever bearer is active. Returns 0 on success. */
int conn_mgr_send(const struct ble_sensor_payload *bin,
		  const char *json, size_t json_len);

/* Drive the FSM forward to the highest-priority healthy bearer.
 * Called on jam confirmation (down-shift) and on recovery (up-shift).
 */
void trigger_bearer_failover(void);
void attempt_wifi_restore(void);

#endif /* CONN_MGR_SETUP_H */
