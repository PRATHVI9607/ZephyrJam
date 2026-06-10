/*
 * JamShield — jamming detection engine interface.
 *
 * Highest-priority application thread (prio 2). Samples WiFi RSSI + packet
 * loss every 100 ms and runs a dual-metric, hysteretic state machine. On a
 * confirmed jam it invokes a registered callback so the bearer manager can
 * fail over. See PRD.md Section 8.
 */
#ifndef JAM_DETECT_H
#define JAM_DETECT_H

#include <stdint.h>

typedef enum {
	JAM_STATE_CLEAR      = 0,
	JAM_STATE_SUSPECTED  = 1,
	JAM_STATE_CONFIRMED  = 2,
	JAM_STATE_RECOVERING = 3,
} jam_state_t;

/* Adaptive thresholds, tuned at runtime from the LDR (adaptive_threshold.c). */
struct jam_thresholds {
	int8_t   rssi_threshold;  /* dBm — suspect if RSSI below this        */
	uint8_t  loss_threshold;  /* %   — suspect if loss above this        */
	uint8_t  window_size;     /* packets in the loss sliding window      */
	uint32_t confirm_ms;      /* sustained degradation before CONFIRMED  */
};

/* Start the detection thread. Returns 0 on success. */
int jam_detect_init(void);

/* Current state / metrics (thread-safe scalars). */
jam_state_t jam_detect_get_state(void);
int8_t      jam_detect_get_rssi(void);
uint8_t     jam_detect_get_loss_pct(void);

/* Register a callback fired on every state change (from detection thread ctx). */
void jam_detect_register_callback(void (*cb)(jam_state_t new_state));

/* Packet accounting, called by the bearer/payload layer for loss tracking. */
void jam_detect_record_sent(void);
void jam_detect_record_acked(void);

/* ---- adaptive_threshold.c ---------------------------------------------- */
/* Recompute thresholds from the latest LDR reading (PRD.md Section 6.5). */
void update_thresholds_from_ldr(uint16_t ldr_adc_value);

/* Shared threshold block (defined in adaptive_threshold.c). */
extern struct jam_thresholds js_thresholds;

#endif /* JAM_DETECT_H */
