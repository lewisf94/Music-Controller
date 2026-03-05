#include <Arduino.h>
#include <TFT_eSPI.h>
#include "ui.h"
#include "spotify.h"

extern TFT_eSPI tft;
extern bool get_touch_coords(int16_t* x, int16_t* y);

// Sprite for double buffering to prevent flickering
TFT_eSprite spr = TFT_eSprite(&tft);
static bool sprite_created = false;

#define ALBUM_COUNT 6
#define ALBUM_SIZE  140
#define ALBUM_GAP   20
#define SCREEN_W    320
#define SCREEN_H    240

// Colors
static uint16_t album_colors[ALBUM_COUNT] = {
    tft.color565(231, 76,  60),
    tft.color565(52,  152, 219),
    tft.color565(46,  204, 113),
    tft.color565(243, 156, 18),
    tft.color565(155, 89,  182),
    tft.color565(26,  188, 156),
};

static const char* album_names[ALBUM_COUNT] = {
    "Rumours",
    "OK Computer",
    "Dark Side",
    "Abbey Road",
    "Nevermind",
    "Kind of Blue",
};

static const char* album_uris[ALBUM_COUNT] = {
    "spotify:album:1bt6q2SruMsDkOWCNLXVW2",
    "spotify:album:6dVIqQ8qmQ5GBnJ9s5QvGg",
    "spotify:album:4FR8Z6TvIsC56NLyNomNRE",
    "spotify:album:0ETFjACtuP2ADo6LFhL6HN",
    "spotify:album:2guirTSEqLizK7j9i1MTTZ",
    "spotify:album:1kbwkEYzzPiJki3tLhE1R3"
};

// --- Physics State ---
static int32_t scroll_x = 0;
static int32_t target_scroll_x = 0;
static int32_t min_scroll = 0;
static int32_t max_scroll = (ALBUM_COUNT - 1) * (ALBUM_SIZE + ALBUM_GAP);

// --- Touch State ---
static bool is_dragging = false;
static int16_t touch_start_x = 0;
static int32_t scroll_start_x = 0;
static unsigned long touch_start_time = 0;
static int32_t momentum_velocity = 0;
static int16_t last_tx = 0;

static void snap_to_closest_album() {
    int closest_index = round((float)scroll_x / (ALBUM_SIZE + ALBUM_GAP));
    if (closest_index < 0) closest_index = 0;
    if (closest_index >= ALBUM_COUNT) closest_index = ALBUM_COUNT - 1;

    target_scroll_x = closest_index * (ALBUM_SIZE + ALBUM_GAP);
}

static void drawAlbumArtPlaceholder(int x, int y, int size, uint16_t color, int index) {
    // Fill background
    spr.fillRoundRect(x, y, size, size, 12, color);
    
    // Draw abstract geometric shapes to represent album art
    uint16_t white = tft.color565(255, 255, 255);
    uint16_t black = tft.color565(0, 0, 0);

    switch(index % 6) {
        case 0: // "Vinyl/Record" style
            spr.fillCircle(x + size/2, y + size/2, size/3, black);
            spr.fillCircle(x + size/2, y + size/2, size/10, color);
            break;
        case 1: // "Window" style
            spr.fillRect(x + size/4, y + size/4, size/5, size/5, white);
            spr.fillRect(x + size/2 + size/20, y + size/4, size/5, size/5, white);
            spr.fillRect(x + size/4, y + size/2 + size/20, size/5, size/5, white);
            spr.fillRect(x + size/2 + size/20, y + size/2 + size/20, size/5, size/5, white);
            break;
        case 2: // "Prism" style
            spr.fillTriangle(x + size/2, y + size/4, x + size/4, y + size*3/4, x + size*3/4, y + size*3/4, white);
            spr.drawLine(x, y + size/2, x + size/2, y + size/4, white);
            break;
        case 3: // "Stripes" style
            spr.fillRect(x + size*0.2, y + size*0.2, size*0.15, size*0.6, white);
            spr.fillRect(x + size*0.45, y + size*0.2, size*0.15, size*0.6, black);
            spr.fillRect(x + size*0.7, y + size*0.2, size*0.15, size*0.6, white);
            break;
        case 4: // "Abstract Waves" style
            for(int i=0; i<5; i++) {
                spr.drawWideLine(x + size/6, y + size/5 + (i*size/7), x + size*5/6, y + size/5 + (i*size/7) + (i%2==0?10:-10), 3, white);
            }
            break;
        case 5: // "Blocks" style
            spr.fillRoundRect(x + size*0.2, y + size*0.2, size*0.6, size*0.3, 4, white);
            spr.fillRoundRect(x + size*0.5, y + size*0.6, size*0.3, size*0.2, 4, black);
            spr.fillRoundRect(x + size*0.2, y + size*0.6, size*0.2, size*0.2, 4, black);
            break;
    }
}

static void draw_ui() {
    if (!sprite_created) {
        // Use 16-bit color depth to fix edge artifacting.
        spr.setColorDepth(16); 
        
        // CRITICAL FIX: ESP32 has a ~110KB contiguous RAM limit.
        // 320 * (ALBUM_SIZE+40) * 2 bytes = 115KB -> FAILS (Black screen).
        // 320 * ALBUM_SIZE (140) * 2 bytes = 89.6KB -> SUCCEEDS!
        if (spr.createSprite(SCREEN_W, ALBUM_SIZE) == nullptr) {
            Serial.println("SPRITE ALLOCATION FAILED");
            return;
        }
        sprite_created = true;
    }

    // 1. Draw all albums into the off-screen sprite
    spr.fillSprite(TFT_BLACK);
    for (int i = 0; i < ALBUM_COUNT; i++) {
        int x_pos = (SCREEN_W / 2 - ALBUM_SIZE / 2) + (i * (ALBUM_SIZE + ALBUM_GAP)) - scroll_x;
        
        // Only draw if physically on screen
        if (x_pos + ALBUM_SIZE > 0 && x_pos < SCREEN_W) {
            drawAlbumArtPlaceholder(x_pos, 0, ALBUM_SIZE, album_colors[i], i);
        }
    }
    
    // 2. Push the completed album graphics to the screen (no tearing)
    int y_offset = (SCREEN_H - ALBUM_SIZE) / 2;
    spr.pushSprite(0, y_offset);

    // 3. Draw text and UI directly to the TFT below the albums
    // First clear just the text area to prevent smearing
    tft.fillRect(0, y_offset + ALBUM_SIZE, SCREEN_W, 40, TFT_BLACK);
    
    for (int i = 0; i < ALBUM_COUNT; i++) {
        int x_pos = (SCREEN_W / 2 - ALBUM_SIZE / 2) + (i * (ALBUM_SIZE + ALBUM_GAP)) - scroll_x;
        if (x_pos + ALBUM_SIZE > 0 && x_pos < SCREEN_W) {
            tft.setTextColor(TFT_WHITE);
            tft.setTextDatum(TC_DATUM);
            tft.drawString(album_names[i], x_pos + ALBUM_SIZE / 2, y_offset + ALBUM_SIZE + 10, 2);
        }
    }
}

void ui_init() {
    snap_to_closest_album();
    draw_ui();
}

void ui_update() {
    static int32_t last_scroll_x = -1;
    int16_t tx, ty;
    bool touched = get_touch_coords(&tx, &ty);

    if (touched) {
        if (!is_dragging) {
            is_dragging = true;
            touch_start_x = tx;
            last_tx = tx;
            scroll_start_x = scroll_x;
            touch_start_time = millis();
            momentum_velocity = 0;
        } else {
            // Actively Dragging - lock 1:1 with finger
            int16_t dx = tx - touch_start_x;
            scroll_x = scroll_start_x - dx;

            // Calculate flick speed
            momentum_velocity = last_tx - tx;
            last_tx = tx;

            // Soft boundaries
            if (scroll_x < min_scroll - 100) scroll_x = min_scroll - 100;
            if (scroll_x > max_scroll + 100) scroll_x = max_scroll + 100;
            
            // Overwrite target so momentum physics don't fight the user's thumb
            target_scroll_x = scroll_x; 
        }
    } else {
        if (is_dragging) {
            is_dragging = false;

            unsigned long duration = millis() - touch_start_time;
            
            // Determine if it was a Tap
            if (abs(scroll_x - scroll_start_x) < 15 && duration < 300) {
                for (int i = 0; i < ALBUM_COUNT; i++) {
                    int x_pos = (SCREEN_W / 2 - ALBUM_SIZE / 2) + (i * (ALBUM_SIZE + ALBUM_GAP)) - scroll_x;
                    // Note: Touch coordinates are relative to the whole screen!
                    if (tx >= x_pos && tx <= x_pos + ALBUM_SIZE && ty >= (SCREEN_H - ALBUM_SIZE) / 2 && ty <= (SCREEN_H + ALBUM_SIZE) / 2) {
                        Serial.print("Tapped Album: ");
                        Serial.println(album_names[i]);
                        spotify_play_album(album_uris[i]);
                        break;
                    }
                }
            } else {
                // It was a drag: Apply flick momentum before snapping
                target_scroll_x = scroll_x + (momentum_velocity * 10);
            }

            // Enforce hard boundaries on the target
            if (target_scroll_x < min_scroll) target_scroll_x = min_scroll;
            if (target_scroll_x > max_scroll) target_scroll_x = max_scroll;

            // Find closest album to the thrown target
            int closest_index = round((float)target_scroll_x / (ALBUM_SIZE + ALBUM_GAP));
            if (closest_index < 0) closest_index = 0;
            if (closest_index >= ALBUM_COUNT) closest_index = ALBUM_COUNT - 1;
            target_scroll_x = closest_index * (ALBUM_SIZE + ALBUM_GAP);
        }
    }

    // Apply smooth momentum towards target
    if (!is_dragging && scroll_x != target_scroll_x) {
        int32_t diff = target_scroll_x - scroll_x;
        scroll_x += diff / 4; // Approach speed
        
        if (abs(diff) < 2) {
            scroll_x = target_scroll_x;
        }
    }

    // Cap framerate to ~60FPS (16ms per frame) to prevent screen tearing over SPI
    static unsigned long last_frame_time = 0;
    if (millis() - last_frame_time >= 16) {
        if (scroll_x != last_scroll_x) {
            draw_ui();
            last_scroll_x = scroll_x;
            last_frame_time = millis();
        }
    }
}

void ui_show_album_browser() {}
void ui_show_now_playing()   {}