/*
 * JamShield — ESP-NOW tertiary bearer shim (PRD.md Section 7.3).
 *
 * ESP-NOW is connectionless (no association/DHCP), making it the hardest tier
 * to jam. Zephyr has no native ESP-NOW L2, so this wraps the Espressif HAL.
 *
 * Build modes:
 *   CONFIG_JS_ESPNOW=y -> real esp_now_send() path (requires the Espressif
 *                          esp_now/esp_wifi HAL headers in this Zephyr port).
 *   CONFIG_JS_ESPNOW=n -> compile-safe logging stub (default). The build stays
 *                          green and WiFi->BLE failover is unaffected.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "espnow_l2.h"

LOG_MODULE_REGISTER(espnow_l2, LOG_LEVEL_INF);

/* Broadcast: any ESP-NOW receiver on the channel hears it (the JamShield
 * ESP-NOW receiver board listens promiscuously; no need to know its MAC). */
static const uint8_t rpi4_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool espnow_ready;

#if defined(CONFIG_JS_ESPNOW)

#include <esp_now.h>
#include <esp_wifi.h>

/* TX-only: the node sends ESP-NOW; a separate receiver board hears it. No recv
 * callback (its signature differs across esp-idf versions and we don't need it). */

int espnow_l2_init(void)
{
	esp_err_t ret = esp_now_init();

	if (ret != ESP_OK) {
		LOG_ERR("esp_now_init failed: %d", (int)ret);
		return -EIO;
	}

	esp_now_peer_info_t peer = {0};

	peer.channel = 0;            /* current channel */
	peer.ifidx = WIFI_IF_STA;
	peer.encrypt = false;
	memcpy(peer.peer_addr, rpi4_mac, sizeof(rpi4_mac));

	ret = esp_now_add_peer(&peer);
	if (ret != ESP_OK) {
		LOG_ERR("esp_now_add_peer failed: %d", (int)ret);
		return -EIO;
	}

	espnow_ready = true;
	LOG_INF("ESP-NOW ready, peer %02x:%02x:%02x:%02x:%02x:%02x",
		rpi4_mac[0], rpi4_mac[1], rpi4_mac[2], rpi4_mac[3],
		rpi4_mac[4], rpi4_mac[5]);
	return 0;
}

int espnow_send_payload(const struct ble_sensor_payload *payload)
{
	if (!espnow_ready || !payload) {
		return -ENOTCONN;
	}

	esp_err_t ret = esp_now_send(rpi4_mac, (const uint8_t *)payload,
				     sizeof(*payload));
	if (ret != ESP_OK) {
		LOG_ERR("esp_now_send failed: %d", (int)ret);
		return -EIO;
	}
	return 0;
}

#else /* !CONFIG_JS_ESPNOW — compile-safe stub */

int espnow_l2_init(void)
{
	LOG_INF("ESP-NOW disabled (CONFIG_JS_ESPNOW=n); tertiary bearer inactive");
	espnow_ready = false;
	return -ENOTSUP;
}

int espnow_send_payload(const struct ble_sensor_payload *payload)
{
	ARG_UNUSED(payload);
	return -ENOTSUP;
}

#endif /* CONFIG_JS_ESPNOW */

bool espnow_l2_is_ready(void)
{
	return espnow_ready;
}
