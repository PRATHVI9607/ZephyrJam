/*
 * JamShield — BLE GATT peripheral (secondary bearer).
 *
 * Advertises "JamShield" from boot and exposes a NOTIFY characteristic that
 * streams the 18-byte ble_sensor_payload. Advertising starts before any jam is
 * confirmed so the RPi4 can pre-connect and cut failover latency.
 * See PRD.md Section 7.2.
 */
#ifndef BLE_GATT_H
#define BLE_GATT_H

#include <stdbool.h>
#include "jamshield.h"

/* Enable BT, register the service, start advertising. Returns 0 on success. */
int ble_gatt_init(void);

/* Send one notification with the given payload. Returns 0 on success,
 * -ENOTCONN if no central is subscribed.
 */
int ble_gatt_send(const struct ble_sensor_payload *payload);

bool ble_gatt_is_connected(void);
bool ble_gatt_notifications_enabled(void);

#endif /* BLE_GATT_H */
