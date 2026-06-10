/*
 * JamShield — application entry point.
 *
 * Boot/init order (PRD.md Section 6.1):
 *   1. BLE advertising            (start early so RPi4 can pre-connect)
 *   2. ESP-NOW shim               (tertiary bearer, no-op if disabled)
 *   3. WiFi + MQTT connect        (primary bearer)
 *   4. LDR sensor thread          (feeds adaptive thresholds)
 *   5. Bearer manager             (arms the jam->failover callback)
 *   6. Jamming detection thread   (highest priority)
 *   7. Payload thread             (build + send sensor packets)
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jamshield.h"
#include "sensor_ldr.h"
#include "jam_detect.h"
#include "wifi_mqtt.h"
#include "ble_gatt.h"
#include "espnow_l2.h"
#include "conn_mgr_setup.h"

LOG_MODULE_REGISTER(jamshield, LOG_LEVEL_INF);

/* Defined in payload_thread.c */
int payload_thread_start(void);

int main(void)
{
	LOG_INF("==== JamShield starting ====");
	LOG_INF("Board: %s", CONFIG_BOARD);

	if (ble_gatt_init() != 0) {
		LOG_ERR("BLE init failed (continuing without BLE bearer)");
	}

	(void)espnow_l2_init(); /* -ENOTSUP if CONFIG_JS_ESPNOW=n */

	if (wifi_mqtt_connect() != 0) {
		LOG_ERR("WiFi connect kickoff failed");
	}

	if (sensor_ldr_init() != 0) {
		LOG_ERR("LDR sensor init failed");
	}

	(void)conn_mgr_setup_init();
	(void)jam_detect_init();
	(void)payload_thread_start();

	LOG_INF("==== JamShield init complete ====");

	/* Idle: all work runs in dedicated threads. */
	while (1) {
		k_sleep(K_SECONDS(60));
		LOG_INF("alive: bearer=%s jam=%d rssi=%d loss=%u%% up=%llus",
			js_channel_str(conn_mgr_active_bearer()),
			jam_detect_get_state(), jam_detect_get_rssi(),
			jam_detect_get_loss_pct(), k_uptime_get() / 1000);
	}
	return 0;
}
