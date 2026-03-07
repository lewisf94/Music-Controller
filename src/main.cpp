#include "app.h"
#include "input.h"
#include "spotify.h"
#include "ui.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ============================================================
// USER CREDENTIALS - Fill in your details below before building
// ============================================================
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SPOTIFY_CLIENT_ID "YOUR_SPOTIFY_CLIENT_ID"
#define SPOTIFY_CLIENT_SECRET "YOUR_SPOTIFY_CLIENT_SECRET"
#define SPOTIFY_REFRESH_TOKEN "YOUR_SPOTIFY_REFRESH_TOKEN"
// ============================================================

TFT_eSPI tft = TFT_eSPI();
bool sd_ok = false; // Global: true if SD card mounted OK

// --- Rotary Encoder (CLK=IO27, DT=IO22) — polling-based ---
#define ENCODER_CLK 27
#define ENCODER_DT 22
static int32_t encoder_count = 0;
static int lastCLK = HIGH;

// Call every loop iteration to poll encoder
static int32_t enc_lifetime = 0; // Never reset — for debugging
static unsigned long last_enc_time = 0;
#define ENCODER_DEBOUNCE_US 2000 // 2ms debounce

void encoder_poll() {
  int curCLK = digitalRead(ENCODER_CLK);
  if (lastCLK == HIGH && curCLK == LOW) {
    unsigned long now_us = micros();
    if (now_us - last_enc_time > ENCODER_DEBOUNCE_US) {
      // CLK falling edge: check DT to determine direction
      if (digitalRead(ENCODER_DT)) {
        encoder_count++;
        enc_lifetime++;
      } else {
        encoder_count--;
        enc_lifetime--;
      }
      last_enc_time = now_us;
    }
  }
  lastCLK = curCLK;
}

// Called by ui.cpp to consume the accumulated encoder clicks
int32_t get_encoder_delta() {
  int32_t val = encoder_count;
  encoder_count = 0;
  return val;
}

// --- CYD Custom Touch Pins ---
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchSpi = SPIClass(HSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static int16_t smooth_x = -1, smooth_y = -1;
int16_t touch_pressure = 0; // For debug display

bool get_touch_coords(int16_t *x, int16_t *y) {
  if (!touch.tirqTouched() || !touch.touched()) {
    smooth_x = -1;
    smooth_y = -1;
    return false;
  }

  TS_Point p = touch.getPoint();
  touch_pressure = p.z;

  // Map raw XPT2046 coordinates to screen pixels
  int16_t mapped_x = map(p.x, 200, 3800, 0, 319);
  int16_t mapped_y = map(p.y, 200, 3800, 0, 239);
  mapped_x = constrain(mapped_x, 0, 319);
  mapped_y = constrain(mapped_y, 0, 239);

  // Exponential smoothing to reduce jitter
  if (smooth_x < 0) {
    smooth_x = mapped_x;
    smooth_y = mapped_y;
  } else {
    smooth_x = (smooth_x * 3 + mapped_x) / 4;
    smooth_y = (smooth_y * 3 + mapped_y) / 4;
  }

  *x = smooth_x;
  *y = smooth_y;
  return true;
}

void setup() {
  Serial.begin(115200);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSpi);
  touch.setRotation(3);

  // SD card on default VSPI, CS = GPIO 5
  if (SD.begin(5)) {
    sd_ok = true;
    Serial.println("SD card mounted OK");
  } else {
    Serial.println("SD card mount FAILED (continuing without)");
  }

  // Rotary encoder setup (polling, no interrupts)
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  Serial.println("Rotary encoder ready (CLK=27, DT=22, polling)");

  ui_init();
  app_init();

  // Only connect to WiFi/Spotify if real credentials are set
  if (String(WIFI_SSID) != "YOUR_WIFI_SSID") {
    spotify_init(WIFI_SSID, WIFI_PASSWORD, SPOTIFY_CLIENT_ID,
                 SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN);
  } else {
    Serial.println("WiFi skipped (placeholder credentials)");
  }
}

void loop() {
  encoder_poll(); // Read encoder every loop iteration
  input_update();
  spotify_update();
  ui_update();

  // Debug: show lifetime encoder total and touch status
  static unsigned long last_debug = 0;
  if (millis() - last_debug > 500) {
    last_debug = millis();
    int16_t dummy_x, dummy_y;
    bool t = get_touch_coords(&dummy_x, &dummy_y);
    Serial.print("enc_life=");
    Serial.print(enc_lifetime);
    Serial.print(" T=");
    Serial.println(t);
  }
}