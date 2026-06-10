/*
 * JamShield — BLE GATT peripheral (PRD.md Section 7.2).
 *
 * Advertises "JamShield" from boot (connectable) so the RPi4 central can
 * pre-connect before any failover. Exposes:
 *   Service 0x1234
 *     Char 0x1235 (NOTIFY | READ) -> 18-byte ble_sensor_payload
 * On failover the bearer manager simply starts calling ble_gatt_send().
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "ble_gatt.h"

LOG_MODULE_REGISTER(ble_gatt, LOG_LEVEL_INF);

#define BT_UUID_JS_SERVICE BT_UUID_DECLARE_16(0x1234)
#define BT_UUID_JS_SENSOR  BT_UUID_DECLARE_16(0x1235)

static volatile bool notify_enabled;
static struct bt_conn *current_conn;

/* Cached last payload, served on a GATT READ. */
static struct ble_sensor_payload last_payload;

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("BLE notifications %s", notify_enabled ? "enabled" : "disabled");
}

static ssize_t read_sensor(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	ARG_UNUSED(conn);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &last_payload,
				 sizeof(last_payload));
}

/* attrs[0]=service, [1]=char decl, [2]=char value, [3]=CCC */
BT_GATT_SERVICE_DEFINE(js_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_JS_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_JS_SENSOR,
			       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_sensor, NULL, &last_payload),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, JS_BT_DEVICE_NAME,
		sizeof(JS_BT_DEVICE_NAME) - 1),
};

static int start_adv(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		LOG_ERR("Advertising start failed: %d", err);
	} else {
		LOG_INF("BLE advertising as '%s'", JS_BT_DEVICE_NAME);
	}
	return err;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_WRN("BLE connection failed: %u", err);
		return;
	}
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	current_conn = bt_conn_ref(conn);
	LOG_INF("BLE connected: %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	LOG_WRN("BLE disconnected (reason %u), restarting adv", reason);
	notify_enabled = false;
	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
	(void)start_adv();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int ble_gatt_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
		return err;
	}
	LOG_INF("Bluetooth initialized");
	return start_adv();
}

int ble_gatt_send(const struct ble_sensor_payload *payload)
{
	if (!payload) {
		return -EINVAL;
	}

	last_payload = *payload;

	if (!current_conn || !notify_enabled) {
		return -ENOTCONN;
	}

	/* attrs[2] is the characteristic value attribute. */
	int ret = bt_gatt_notify(current_conn, &js_svc.attrs[2], payload,
				 sizeof(*payload));
	if (ret) {
		LOG_DBG("bt_gatt_notify failed: %d", ret);
	}
	return ret;
}

bool ble_gatt_is_connected(void)
{
	return current_conn != NULL;
}

bool ble_gatt_notifications_enabled(void)
{
	return notify_enabled;
}
