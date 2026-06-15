/*
 * JamShield — WiFi power-save shim.
 *
 * The Zephyr esp32 driver doesn't implement NET_REQUEST_WIFI_PS (returns
 * -ENOTSUP), so we disable power-save via the Espressif HAL directly. This MUST
 * be in its own translation unit: esp_wifi.h and zephyr/net/wifi_mgmt.h define
 * clashing types, so it can't live in wifi_mqtt.c.
 *
 * With power-save on, the station sleeps between its own transmissions and the
 * AP drops buffered downlink frames — uplink (MQTT publish) works but downlink
 * (the JAM/CLEAR control messages) is missed, especially on phone hotspots.
 */
#include <esp_wifi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esp_ps, LOG_LEVEL_INF);

void js_wifi_disable_ps(void)
{
	esp_err_t e = esp_wifi_set_ps(WIFI_PS_NONE);

	LOG_INF("esp_wifi_set_ps(NONE) = %d", (int)e);
}
