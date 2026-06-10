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
#include <zephyr/net/net_if.h>
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

#define WIFI_RETRY_MS 5000

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
static struct zsock_pollfd fds[1];

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
	case NET_EVENT_L4_CONNECTED:
		wifi_l4_up = true;
		LOG_INF("WiFi L4 connected (IP up) at t=%lld ms", k_uptime_get());
		break;
	case NET_EVENT_L4_DISCONNECTED:
		wifi_l4_up = false;
		mqtt_connected = false;
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
		if (evt->result == 0) {
			mqtt_connected = true;
			LOG_INF("MQTT connected to broker %s", JS_MQTT_BROKER_IP);
		} else {
			LOG_ERR("MQTT CONNACK error %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		mqtt_connected = false;
		LOG_WRN("MQTT disconnected");
		break;
	case MQTT_EVT_PUBACK:
		/* QoS1 acknowledged -> counts as a delivered packet. */
		jam_detect_record_acked();
		break;
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
	if (wifi_iface && !wifi_l4_up && !associating) {
		uint64_t now = k_uptime_get();

		if (last_connect_attempt == 0 ||
		    (now - last_connect_attempt) > WIFI_RETRY_MS) {
			(void)do_wifi_connect();
		}
	}

	/* Establish MQTT once WiFi (L4) is up. */
	if (wifi_l4_up && !mqtt_connected) {
		if (mqtt_session_start() != 0) {
			return;
		}
	}

	if (!mqtt_connected && client.transport.tcp.sock <= 0) {
		return;
	}

	/* Service the socket and keepalive. */
	int rc = zsock_poll(fds, 1, 0);

	if (rc > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
		(void)mqtt_input(&client);
	}
	(void)mqtt_live(&client);
}

bool wifi_mqtt_is_connected(void)
{
	return wifi_l4_up && mqtt_connected;
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
