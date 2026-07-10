/*
 * JamShield — LDR (light dependent resistor) sensor interface.
 *
 * Reads GPIO34 / ADC1_CH6 every 1000 ms on a dedicated thread and exposes the
 * latest sample in a thread-safe way. All arithmetic is fixed-point integer —
 * no float in any thread (Xtensa LX6 FP rules, see CLAUDE.md critical rule 1).
 */
#ifndef SENSOR_LDR_H
#define SENSOR_LDR_H

#include <stdint.h>

struct ldr_reading {
	uint16_t adc_raw;      /* 0-4095, 12-bit */
	uint32_t timestamp_ms; /* k_uptime_get() at read time */
};

/* Initialize the ADC channel and start the sensor thread. Returns 0 on ok. */
int sensor_ldr_init(void);

/* Latest reading (thread-safe copy). */
struct ldr_reading sensor_ldr_get(void);

/* Approximate lux scaled x10 (one decimal of precision, integer only). */
uint32_t sensor_ldr_to_lux_x10(uint16_t adc_raw);

#endif /* SENSOR_LDR_H */
