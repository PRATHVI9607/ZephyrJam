/*
 * JamShield — WiFi station + MQTT publisher (PRD.md Section 7.1).
 *
 * Associates with the RPi4 hostapd network and connects to the Mosquitto
 * broker over TCP. WiFi association is event-driven (net_mgmt L4 events); the
 * MQTT session is (re)established lazily from wifi_mqtt_process(), which the
 * payload thread pumps every cycle to also service keepalive/RX.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>

#include "wifi_mqtt.h"
#include "jamshield.h"
#include "jam_detect.h"

LOG_MODULE_REGISTER(wifi_mqtt, LOG_LEVEL_INF);

/* ---- WiFi state --------------------------------------------------------- */
static struct net_if *wifi_iface;
static volatile bool wifi_l4_up;
static volatile bool associating;
static volatile int8_t last_rssi;
static uint64_t last_connect_attempt;

#define WIFI_RETRY_MS    2000
#define ASSOC_TIMEOUT_MS 8000

static void wifi_disable_ps(void);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback l4_cb;

#define L4_EVENTS   (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define WIFI_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

/* ---- MQTT state --------------------------------------------------------- */
static struct mqtt_client client;
static struct sockaddr_storage broker;
static uint8_t rx_buffer[512];
static uint8_t tx_buffer[512];
static volatile bool mqtt_connected;
static volatile bool mqtt_attempting;
static volatile bool mqtt_subscribed;
static volatile bool ever_connected;
static volatile bool force_jam;
static uint64_t mqtt_attempt_ms;
static struct zsock_pollfd fds[1];

#define MQTT_CONNACK_TIMEOUT_MS 5000
#define JS_CONTROL_TOPIC        "jamshield/control"
/* A forced jam auto-expires so the node recovers even when the inbound link is
 * unavailable during BLE failover (and as a safety bound for experiments). */
#define FORCE_JAM_MAX_MS        15000

static uint64_t force_jam_ms;

bool wifi_mqtt_force_jam(void)
{
	if (force_jam && (k_uptime_get() - force_jam_ms) > FORCE_JAM_MAX_MS) {
		force_jam = false;
	}
	return force_jam;
}

void wifi_mqtt_toggle_jam(void)
{
	force_jam = !force_jam;
	force_jam_ms = k_uptime_get();
	LOG_WRN("BOOT button -> force_jam=%d", force_jam);
}

void wifi_mqtt_set_jam(bool on)
{
	force_jam = on;
	force_jam_ms = k_uptime_get();
}

static struct net_if *get_wifi_iface(void)
{
#if defined(CONFIG_WIFI)
	struct net_if *iface = net_if_get_first_wifi();

	if (iface) {
		return iface;
	}
#endif
	return net_if_get_default();
}

/* ---- net_mgmt event handlers ------------------------------------------- */
static void l4_evt_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			   struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	switch (event) {
	case NET_EVENT_L4_CONNECTED: {
		wifi_l4_up = true;
		struct in_addr *a = wifi_iface ?
			net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED) :
			NULL;
		char ip[NET_IPV4_ADDR_LEN] = "none";

		if (a) {
			(void)net_addr_ntop(AF_INET, a, ip, sizeof(ip));
		}
		LOG_INF("WiFi L4 connected: IP=%s, broker=%s:%d (t=%lld ms)",
			ip, JS_MQTT_BROKER_IP, JS_MQTT_BROKER_PORT,
			k_uptime_get());
		break;
	}
	case NET_EVENT_L4_DISCONNECTED:
		wifi_l4_up = false;
		mqtt_connected = false;
		mqtt_attempting = false;
		mqtt_subscribed = false; /* must re-subscribe after reconnect */
		LOG_WRN("WiFi L4 disconnected at t=%lld ms", k_uptime_get());
		break;
	default:
		break;
	}
}

static void wifi_evt_handler(struct net_mgmt_event_callback *cb, uint32_t event,
			     struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;

		associating = false;
		if (st && st->status) {
			LOG_WRN("WiFi connect failed (status %d), will retry",
				st->status);
		} else {
			LOG_INF("WiFi associated to %s", JS_WIFI_SSID);
			wifi_disable_ps();
		}
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		wifi_l4_up = false;
		associating = false;
		LOG_WRN("WiFi disassociated");
	}
}

/* ---- WiFi connect ------------------------------------------------------- */
static int do_wifi_connect(void)
{
	static struct wifi_connect_req_params params;

	params.ssid = (const uint8_t *)JS_WIFI_SSID;
	params.ssid_length = sizeof(JS_WIFI_SSID) - 1;
	params.psk = (const uint8_t *)JS_WIFI_PSK;
	params.psk_length = sizeof(JS_WIFI_PSK) - 1;
	params.security = WIFI_SECURITY_TYPE_PSK;
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_OPTIONAL;

	/* The esp32 driver only accepts a connect once the iface is up and in
	 * station mode; bring it up first (idempotent).
	 */
	if (!net_if_is_admin_up(wifi_iface)) {
		int up = net_if_up(wifi_iface);

		if (up) {
			LOG_WRN("net_if_up failed: %d", up);
		}
	}

	associating = true;
	last_connect_attempt = k_uptime_get();

	int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &params,
			   sizeof(params));
	if (ret) {
		associating = false;
		LOG_WRN("WiFi connect request rejected (%d), will retry", ret);
	}
	return ret;
}

/* Disable WiFi power-save: keeps the radio awake so downlink (broker -> ESP32,
 * e.g. the JAM/CLEAR control messages) is delivered reliably and latency is low.
 * Without this, the station sleeps between its own TX and the AP drops buffered
 * downlink packets — uplink (publish) works but downlink is missed.
 */
/* Implemented in esp_ps_shim.c (calls esp_wifi_set_ps(WIFI_PS_NONE)). */
extern void js_wifi_disable_ps(void);

static void wifi_disable_ps(void)
{
	js_wifi_disable_ps();
}

int wifi_mqtt_connect(void)
{
	wifi_iface = get_wifi_iface();
	if (!wifi_iface) {
		LOG_ERR("No WiFi interface found");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&l4_cb, l4_evt_handler, L4_EVENTS);
	net_mgmt_add_event_callback(&l4_cb);
	net_mgmt_init_event_callback(&wifi_cb, wifi_evt_handler, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);

	/* Bring the station interface up; the periodic retry in
	 * wifi_mqtt_process() drives association so we tolerate the AP not
	 * being present yet and recover automatically when it appears.
	 */
	(void)net_if_up(wifi_iface);
	LOG_INF("WiFi station up; will associate to SSID '%s'", JS_WIFI_SSID);
	return 0;
}

void wifi_mqtt_disconnect(void)
{
	if (mqtt_connected) {
		(void)mqtt_disconnect(&client);
		mqtt_connected = false;
	}
	if (wifi_iface) {
		(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_iface, NULL, 0);
	}
	wifi_l4_up = false;
}

/* ---- MQTT --------------------------------------------------------------- */
static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	ARG_UNUSED(c);

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		mqtt_attempting = false;
		if (evt->result == 0) {
			mqtt_connected = true;
			ever_connected = true;
			LOG_INF("MQTT connected to broker %s", JS_MQTT_BROKER_IP);
		} else {
			LOG_ERR("MQTT CONNACK error %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		mqtt_connected = false;
		mqtt_attempting = false;
		mqtt_subscribed = false;
		LOG_WRN("MQTT disconnected");
		break;
	case MQTT_EVT_PUBACK:
		/* QoS1 acknowledged -> counts as a delivered packet. */
		jam_detect_record_acked();
		break;
	case MQTT_EVT_SUBACK:
		LOG_INF("Subscribed to %s", JS_CONTROL_TOPIC);
		break;
	case MQTT_EVT_PUBLISH: {
		/* Control message: "JAM" forces failover, "CLEAR" recovers. */
		const struct mqtt_publish_param *pp = &evt->param.publish;
		uint8_t buf[32];
		uint32_t len = pp->message.payload.len;

		if (len >= sizeof(buf)) {
			len = sizeof(buf) - 1;
		}
		int r = mqtt_read_publish_payload(c, buf, len);

		if (r > 0) {
			buf[r] = '\0';
			if (strstr((char *)buf, "JAM")) {
				force_jam = true;
				force_jam_ms = k_uptime_get();
			} else if (strstr((char *)buf, "CLEAR")) {
				force_jam = false;
			}
			LOG_WRN("control rx '%s' -> force_jam=%d", buf, force_jam);
		}
		break;
	}
	default:
		break;
	}
}

static void broker_init(void)
{
	struct sockaddr_in *b = (struct sockaddr_in *)&broker;

	b->sin_family = AF_INET;
	b->sin_port = htons(JS_MQTT_BROKER_PORT);
	(void)zsock_inet_pton(AF_INET, JS_MQTT_BROKER_IP, &b->sin_addr);
}

static int mqtt_session_start(void)
{
	broker_init();
	mqtt_subscribed = false; /* every new MQTT session must re-subscribe */
	mqtt_client_init(&client);

	client.broker = &broker;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (uint8_t *)"jamshield_esp32";
	client.client_id.size = sizeof("jamshield_esp32") - 1;
	client.password = NULL;
	client.user_name = NULL;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.keepalive = CONFIG_MQTT_KEEPALIVE;
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;

	int ret = mqtt_connect(&client);

	if (ret != 0) {
		LOG_ERR("mqtt_connect failed: %d", ret);
		return ret;
	}

	fds[0].fd = client.transport.tcp.sock;
	fds[0].events = ZSOCK_POLLIN;
	return 0;
}

static uint16_t next_msg_id(void)
{
	static uint16_t id;

	if (++id == 0U) {
		id = 1U; /* 0 is not a valid MQTT message id */
	}
	return id;
}

int wifi_mqtt_publish(const char *topic, const char *payload, size_t len)
{
	if (!mqtt_connected) {
		return -ENOTCONN;
	}

	struct mqtt_publish_param param = {0};

	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = (uint8_t *)topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = len;
	param.message_id = next_msg_id();
	param.dup_flag = 0;
	param.retain_flag = 0;

	int ret = mqtt_publish(&client, &param);

	if (ret != 0) {
		LOG_ERR("mqtt_publish failed: %d", ret);
	}
	return ret;
}

void wifi_mqtt_process(void)
{
	/* Drive WiFi (re)association with a simple backoff while not connected. */
	if (wifi_iface && !wifi_l4_up) {
		uint64_t now = k_uptime_get();

		/* Recover from a connect attempt that never returned a result
		 * (can happen during a deauth storm) so we keep retrying. */
		if (associating && (now - last_connect_attempt) > ASSOC_TIMEOUT_MS) {
			associating = false;
		}
		if (!associating &&
		    (last_connect_attempt == 0 ||
		     (now - last_connect_attempt) > WIFI_RETRY_MS)) {
			(void)do_wifi_connect();
		}
	}

	if (!wifi_l4_up) {
		return;
	}

	/* Initiate the MQTT session exactly once, then pump the socket until the
	 * broker's CONNACK arrives (which sets mqtt_connected). Re-initiating
	 * every cycle would leak sockets, so we gate on mqtt_attempting.
	 */
	if (!mqtt_connected && !mqtt_attempting) {
		if (mqtt_session_start() == 0) {
			mqtt_attempting = true;
			mqtt_attempt_ms = k_uptime_get();
		}
		return;
	}

	/* Subscribe to the control topic once, after the session is up. */
	if (mqtt_connected && !mqtt_subscribed) {
		struct mqtt_topic topic = {
			.topic = {
				.utf8 = (uint8_t *)JS_CONTROL_TOPIC,
				.size = sizeof(JS_CONTROL_TOPIC) - 1,
			},
			.qos = MQTT_QOS_0_AT_MOST_ONCE,
		};
		struct mqtt_subscription_list sub = {
			.list = &topic,
			.list_count = 1,
			.message_id = next_msg_id(),
		};

		if (mqtt_subscribe(&client, &sub) == 0) {
			mqtt_subscribed = true;
		}
	}

	/* Service the socket: receive CONNACK/PUBACK and run keepalive. */
	int rc = zsock_poll(fds, 1, 0);

	if (rc > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
		(void)mqtt_input(&client);
	}
	(void)mqtt_live(&client);

	/* If CONNACK never arrives, tear down and retry on the next cycle. */
	if (mqtt_attempting && !mqtt_connected &&
	    (k_uptime_get() - mqtt_attempt_ms) > MQTT_CONNACK_TIMEOUT_MS) {
		LOG_WRN("MQTT CONNACK timeout; retrying");
		(void)mqtt_abort(&client);
		mqtt_attempting = false;
	}
}

bool wifi_mqtt_is_connected(void)
{
	return wifi_l4_up && mqtt_connected;
}

bool wifi_mqtt_was_connected(void)
{
	return ever_connected;
}

int8_t wifi_mqtt_get_rssi(void)
{
	struct wifi_iface_status status = {0};

	if (wifi_iface &&
	    net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, wifi_iface, &status,
		     sizeof(status)) == 0) {
		if (status.rssi != 0) {
			last_rssi = (int8_t)status.rssi;
		}
	}
	return last_rssi;
}
