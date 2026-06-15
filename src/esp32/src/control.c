/*
 * JamShield — USB serial control.
 *
 * Reads single-character commands from the console UART so the dashboard can
 * drive the node reliably over USB (the WiFi downlink is unreliable on some
 * APs/hotspots). Commands:
 *   h = HOP mode (full WiFi->BLE->ESP-NOW failover)
 *   n = NO-HOP mode (stay on WiFi; jammed = packets stop)
 *   e = NO-BLE mode (WiFi->ESP-NOW, skip BLE)
 *   j = force jam ON (manual test)     c = force jam OFF
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "conn_mgr_setup.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(control, LOG_LEVEL_INF);

#define CTRL_STACK_SIZE 1024
#define CTRL_PRIORITY   7

static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static K_THREAD_STACK_DEFINE(ctrl_stack, CTRL_STACK_SIZE);
static struct k_thread ctrl_thread;

static void handle_cmd(uint8_t c)
{
	switch (c) {
	case 'h': case 'H': js_mode_set(JS_MODE_HOP);   break;
	case 'n': case 'N': js_mode_set(JS_MODE_NOHOP); break;
	case 'e': case 'E': js_mode_set(JS_MODE_NOBLE); break;
	case 'j': case 'J': wifi_mqtt_set_jam(true);    break;
	case 'c': case 'C': wifi_mqtt_set_jam(false);   break;
	default: break; /* ignore newlines / noise */
	}
}

static void ctrl_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint8_t ch;

	while (1) {
		while (uart_poll_in(console_dev, &ch) == 0) {
			handle_cmd(ch);
		}
		k_msleep(40);
	}
}

int control_init(void)
{
	if (!device_is_ready(console_dev)) {
		LOG_ERR("console UART not ready for control input");
		return -ENODEV;
	}

	k_thread_create(&ctrl_thread, ctrl_stack,
			K_THREAD_STACK_SIZEOF(ctrl_stack),
			ctrl_fn, NULL, NULL, NULL, CTRL_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ctrl_thread, "control");
	LOG_INF("Serial control ready (h/n/e=mode, j/c=jam)");
	return 0;
}
