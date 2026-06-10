/*
 * JamShield — LDR-adaptive jamming thresholds (PRD.md Section 6.5).
 *
 * The LDR is used as a coarse proxy for ambient activity: a bright room tends
 * to mean people/devices present and naturally noisier RF, so we relax the
 * detector to avoid false positives; a dark/quiet room lets us be more
 * sensitive. Pure integer logic, safe to call from the sensor thread.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jam_detect.h"

LOG_MODULE_REGISTER(adaptive_thr, LOG_LEVEL_INF);

#define LDR_BRIGHT_THRESHOLD 3000 /* ADC: bright room */
#define LDR_DIM_THRESHOLD    1000 /* ADC: dim room */

/* Shared with jam_detect.c. Default = "medium light" tier. */
struct jam_thresholds js_thresholds = {
	.rssi_threshold = -80,
	.loss_threshold = 30,
	.window_size    = 10,
	.confirm_ms     = 300,
};

void update_thresholds_from_ldr(uint16_t ldr_adc_value)
{
	int8_t new_rssi;
	uint8_t new_loss;
	uint32_t new_confirm;

	if (ldr_adc_value > LDR_BRIGHT_THRESHOLD) {
		new_rssi = -85;
		new_loss = 40;
		new_confirm = 400;
	} else if (ldr_adc_value > LDR_DIM_THRESHOLD) {
		new_rssi = -80;
		new_loss = 30;
		new_confirm = 300;
	} else {
		new_rssi = -75;
		new_loss = 20;
		new_confirm = 200;
	}

	/* Only log when something actually changed to keep the serial quiet. */
	if (new_rssi != js_thresholds.rssi_threshold ||
	    new_loss != js_thresholds.loss_threshold) {
		LOG_DBG("thresholds <- ldr=%u: rssi=%d loss=%u%% confirm=%ums",
			ldr_adc_value, new_rssi, new_loss, new_confirm);
	}

	js_thresholds.rssi_threshold = new_rssi;
	js_thresholds.loss_threshold = new_loss;
	js_thresholds.confirm_ms = new_confirm;
}
