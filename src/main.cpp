#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h> // <-- Required for custom SPI routing
#include "ui.h"
#include "app.h"
#include "input.h"

TFT_eSPI tft = TFT_eSPI();

// --- CYD Custom Touch Pins ---
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Create a custom SPI bus for the touch controller
SPIClass touchSpi = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 10];
static lv_color_t buf2[320 * 10];

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
    // static variables remember their value between function calls
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    if (touch.touched()) {
        TS_Point p = touch.getPoint();

        // Map raw touch values to screen coordinates
        last_x = map(p.x, 200, 3700, 0, 320);
        last_y = map(p.y, 240, 3800, 0, 240);

        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    // Always feed LVGL the coordinates, even on release
    data->point.x = last_x;
    data->point.y = last_y;
}

void setup()
{
    Serial.begin(115200);

    tft.begin();
    tft.setRotation(1);

    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 320 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // --- Boot Touch on Custom SPI Bus ---
    touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSpi);
    touch.setRotation(1);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();     // Create UI
    app_init();    // Initialise state
}

void loop()
{
    lv_timer_handler();
    input_update();
}