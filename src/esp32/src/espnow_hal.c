/*
 * JamShield node — ESP-NOW tertiary bearer via raw 802.11 injection.
 *
 * The esp_now_* API does not link in this Zephyr 3.6 ESP32 port, but
 * esp_wifi_80211_tx() does (the jammer uses it). ESP-NOW is just a connectionless
 * 802.11 vendor action frame, so we craft one by hand and inject it — same
 * mechanism, no AP/association to the peer. A JamShield ESP-NOW receiver board
 * sniffs these frames in promiscuous mode.
 *
 * Isolated here because esp_wifi.h clashes with zephyr/net/wifi_mgmt.h.
 */
#include <stdint.h>
#include <string.h>
#include <esp_wifi.h>

/* 24-byte action MAC header + category + Espressif OUI + 4-byte magic + payload. */
static uint8_t frame[24 + 1 + 3 + 4 + 32];

int espnow_hal_send(const uint8_t *payload, int len)
{
	int i = 0;

	if (len > 32) {
		len = 32;
	}

	frame[i++] = 0xd0; frame[i++] = 0x00;          /* FC: action            */
	frame[i++] = 0x00; frame[i++] = 0x00;          /* duration              */
	for (int k = 0; k < 6; k++) frame[i++] = 0xff; /* DA: broadcast         */
	frame[i++] = 0x68; frame[i++] = 0x09;          /* SA: JamShield node    */
	frame[i++] = 0x47; frame[i++] = 0x4a;
	frame[i++] = 0x53; frame[i++] = 0x01;
	for (int k = 0; k < 6; k++) frame[i++] = 0xff; /* BSSID: broadcast      */
	frame[i++] = 0x00; frame[i++] = 0x00;          /* seq                   */

	frame[i++] = 0x7f;                             /* category: vendor      */
	frame[i++] = 0x18; frame[i++] = 0xfe; frame[i++] = 0x34; /* Espressif OUI */
	frame[i++] = 'J'; frame[i++] = 'S'; frame[i++] = 'N'; frame[i++] = '1'; /* magic */

	memcpy(&frame[i], payload, len);
	i += len;

	return (int)esp_wifi_80211_tx(WIFI_IF_STA, frame, i, false);
}
