/*
 * JamShield — ESP-NOW tertiary bearer (PRD.md Section 7.3).
 *
 * ESP-NOW is connectionless (no association/DHCP), making it the hardest tier
 * to jam. Zephyr has no native ESP-NOW L2 and the esp_now_* API doesn't link in
 * this port, so we send the same connectionless 802.11 vendor frame by raw
 * injection (see espnow_hal.c). A JamShield receiver board sniffs it.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "espnow_l2.h"

LOG_MODULE_REGISTER(espnow_l2, LOG_LEVEL_INF);

/* Implemented in espnow_hal.c (isolated esp_wifi.h). */
extern int espnow_hal_send(const uint8_t *payload, int len);

static bool espnow_ready;

int espnow_l2_init(void)
{
	/* Raw injection works once the WiFi radio is up (station path brings it
	 * up). Mark ready; sends report an error if the radio isn't ready yet. */
	espnow_ready = true;
	LOG_INF("ESP-NOW ready (raw 802.11 vendor frames, broadcast)");
	return 0;
}

int espnow_send_payload(const struct ble_sensor_payload *payload)
{
	if (!payload) {
		return -EINVAL;
	}
	int ret = espnow_hal_send((const uint8_t *)payload, sizeof(*payload));

	if (ret != 0) {
		LOG_DBG("esp_wifi_80211_tx ret %d", ret);
		return -EIO;
	}
	return 0;
}

bool espnow_l2_is_ready(void)
{
	return espnow_ready;
}
