#include <Arduino.h>
#include "input.h"
#include "app.h"

// Set to -1 to disable the physical button for now, since pin 22 is used by the encoder DT
#define BTN_PIN -1
static unsigned long last_btn_time = 0;
extern void ui_toggle_view();

void input_init()
{
    if (BTN_PIN >= 0) {
        pinMode(BTN_PIN, INPUT_PULLUP);
    }
}

void input_update()
{
    if (BTN_PIN >= 0) {
        // Basic debounce
        if (millis() - last_btn_time > 200) {
            if (digitalRead(BTN_PIN) == LOW) {
                last_btn_time = millis();
                ui_toggle_view();
            }
        }
    }
}