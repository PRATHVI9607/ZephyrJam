/*
 * JamShield ESP-NOW receiver (Arduino-core ESP32).
 *
 * The Zephyr port doesn't expose esp_wifi promiscuous/esp_now, but the Arduino
 * ESP32 core does. This sniffs the node's ESP-NOW vendor action frames (magic
 * "JSN1", injected by src/esp32/src/espnow_hal.c) on loki's channel and prints
 * each 18-byte payload as one JSON line over USB:
 *
 *   ESPNOW {"seq":N,"val":A,"rssi":R,"jam":J}
 *
 * Flash to the 2nd ESP32 (the jammer board) with Arduino IDE. Then run
 * scripts/espnow_bridge.py to forward these to MQTT so the PWA shows ESP-NOW.
 *
 * Set CH to loki's 2.4 GHz channel (run.ps1 / the AP shows it).
 */
#include <WiFi.h>
#include "esp_wifi.h"
#include <string.h>

#define CH 6   // <-- set to loki's 2.4 GHz channel

static void onRx(void *buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
  const uint8_t *f = p->payload;
  int len = p->rx_ctrl.sig_len;
  const int off = 24 + 1 + 3;                 // MAC hdr + category + OUI
  if (len < off + 4 + 18) return;
  if (f[0] != 0xd0) return;                   // action frame
  if (f[off]!='J'||f[off+1]!='S'||f[off+2]!='N'||f[off+3]!='1') return;

  const uint8_t *b = f + off + 4;             // 18-byte ble_sensor_payload
  uint32_t seq; uint16_t ldr;
  memcpy(&seq, b, 4);                         // seq @0
  memcpy(&ldr, b + 13, 2);                    // ldr_adc @13 (seq4+ts8+ch1)
  int rssi = (int8_t)b[15];
  uint8_t jam = b[16];
  Serial.printf("ESPNOW {\"seq\":%u,\"val\":%u,\"rssi\":%d,\"jam\":%u}\n",
                (unsigned)seq, (unsigned)ldr, rssi, (unsigned)jam);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&onRx);
  esp_wifi_set_channel(CH, WIFI_SECOND_CHAN_NONE);
  Serial.println("JamShield ESP-NOW receiver ready (promiscuous)");
}

void loop() { delay(1000); }
