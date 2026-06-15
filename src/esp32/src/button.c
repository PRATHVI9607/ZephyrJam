/*
 * JamShield — BOOT button jam trigger.
 *
 * The on-board BOOT button (GPIO0) is reused at runtime as a manual,
 * network-independent jam trigger for live demos: one press toggles the forced
 * jam (WiFi -> BLE failover); press again, or wait for the 15 s auto-expire, to
 * recover. This works even when the network can't deliver the MQTT control
 * message (e.g. phone hotspots that drop downlink to the node).
 *
 * NOTE: GPIO0 is a strapping pin — do not hold it during power-on/reset or the
 * chip enters serial-download mode. It is only read after boot.
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback cb_data;
static int64_t last_press_ms;

static void on_press(const struct device *dev, struct gpio_callback *cb,
		     uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int64_t now = k_uptime_get();

	if (now - last_press_ms < 300) {
		return; /* debounce */
	}
	last_press_ms = now;
	wifi_mqtt_toggle_jam();
}

int button_init(void)
{
	if (!gpio_is_ready_dt(&btn)) {
		LOG_ERR("BOOT button GPIO not ready");
		return -ENODEV;
	}

	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&cb_data, on_press, BIT(btn.pin));
	gpio_add_callback(btn.port, &cb_data);

	LOG_INF("BOOT button armed (press = toggle jam)");
	return 0;
}
