#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "ui.h"
#include "app.h"
#include "input.h"
#include "spotify.h"

// ============================================================
// USER CREDENTIALS - Fill in your details below before building
// ============================================================
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#define SPOTIFY_CLIENT_ID     "YOUR_SPOTIFY_CLIENT_ID"
#define SPOTIFY_CLIENT_SECRET "YOUR_SPOTIFY_CLIENT_SECRET"
#define SPOTIFY_REFRESH_TOKEN "YOUR_SPOTIFY_REFRESH_TOKEN"
// ============================================================

TFT_eSPI tft = TFT_eSPI();

// --- CYD Custom Touch Pins ---
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

bool get_touch_coords(int16_t* x, int16_t* y) {
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        // CYD Portrait Touch Calibration
        *x = map(p.x, 200, 3700, 0, 320);
        *y = map(p.y, 240, 3800, 0, 240);
        return true;
    }
    return false;
}

void setup()
{
    Serial.begin(115200);

    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSpi);
    touch.setRotation(1);

    ui_init();     // Our new custom UI engine
    app_init();

    spotify_init(
        WIFI_SSID, 
        WIFI_PASSWORD, 
        SPOTIFY_CLIENT_ID, 
        SPOTIFY_CLIENT_SECRET, 
        SPOTIFY_REFRESH_TOKEN
    );
}

void loop()
{
    input_update();
    spotify_update();
    ui_update(); // We will implement a custom loop hook
}