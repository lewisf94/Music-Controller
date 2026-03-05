#include "ui.h"
#include "spotify.h"
#include <Arduino.h>
#include <TFT_eSPI.h>


extern TFT_eSPI tft;
extern bool get_touch_coords(int16_t *x, int16_t *y);

// Sprite for double buffering to prevent flickering
TFT_eSprite spr = TFT_eSprite(&tft);
static bool sprite_created = false;

#define ALBUM_COUNT 6
#define SCREEN_W 320
#define SCREEN_H 240

// Cover Flow layout constants (tuned for 320x240)
#define CENTER_SIZE 130 // Center album size
#define SIDE_SIZE 80    // ±1 album size
#define FAR_SIZE 50     // ±2 album size
#define SIDE_OFFSET 110 // X distance of ±1 albums from center
#define FAR_OFFSET 170  // X distance of ±2 albums from center
#define SPRITE_H 140    // Sprite height (fits in RAM: 320*140*2 = 89.6KB)

// Colors
static uint16_t album_colors[ALBUM_COUNT] = {
    tft.color565(231, 76, 60),  tft.color565(52, 152, 219),
    tft.color565(46, 204, 113), tft.color565(243, 156, 18),
    tft.color565(155, 89, 182), tft.color565(26, 188, 156),
};

// Darkened colors for side albums (pre-computed to avoid runtime cost)
static uint16_t album_colors_dim[ALBUM_COUNT] = {
    tft.color565(150, 49, 39),  tft.color565(34, 99, 142),
    tft.color565(30, 133, 73),  tft.color565(158, 101, 12),
    tft.color565(101, 58, 118), tft.color565(17, 122, 101),
};

static uint16_t album_colors_far[ALBUM_COUNT] = {
    tft.color565(81, 27, 21), tft.color565(18, 53, 77),
    tft.color565(16, 71, 40), tft.color565(85, 55, 6),
    tft.color565(54, 31, 64), tft.color565(9, 66, 55),
};

static const char *album_names[ALBUM_COUNT] = {
    "Rumours",    "OK Computer", "Dark Side",
    "Abbey Road", "Nevermind",   "Kind of Blue",
};

static const char *album_uris[ALBUM_COUNT] = {
    "spotify:album:1bt6q2SruMsDkOWCNLXVW2",
    "spotify:album:6dVIqQ8qmQ5GBnJ9s5QvGg",
    "spotify:album:4FR8Z6TvIsC56NLyNomNRE",
    "spotify:album:0ETFjACtuP2ADo6LFhL6HN",
    "spotify:album:2guirTSEqLizK7j9i1MTTZ",
    "spotify:album:1kbwkEYzzPiJki3tLhE1R3"};

// --- Scroll State ---
// scroll_pos is in fixed-point: multiply by 100 for precision (0 = album 0, 100
// = album 1, etc.)
static int32_t scroll_pos = 0;    // Current position * 100
static int32_t target_scroll = 0; // Target position * 100
#define SCROLL_SCALE 100
#define DRAG_SENSITIVITY 180 // Pixels of drag per one album scroll

// --- Touch State ---
static bool is_dragging = false;
static int16_t touch_start_x = 0;
static int32_t scroll_start = 0;
static unsigned long touch_start_time = 0;
static int32_t momentum_velocity = 0;
static int16_t last_tx = 0;

// Simple lerp using integer math: result = a + (b-a)*t/100, where t is 0..100
static int32_t iLerp(int32_t a, int32_t b, int32_t t) {
  if (t <= 0)
    return a;
  if (t >= 100)
    return b;
  return a + ((b - a) * t) / 100;
}

static void drawAlbumArtPlaceholder(int x, int y, int size, uint16_t color,
                                    int index) {
  int r = max(2, 12 * size / CENTER_SIZE);
  spr.fillRoundRect(x, y, size, size, r, color);

  uint16_t white = TFT_WHITE;
  uint16_t black = TFT_BLACK;

  int cx = x + size / 2;
  int cy = y + size / 2;

  switch (index % 6) {
  case 0: // Vinyl
    spr.fillCircle(cx, cy, size / 3, black);
    spr.fillCircle(cx, cy, size / 10, color);
    break;
  case 1: // Window
    spr.fillRect(x + size / 4, y + size / 4, size / 5, size / 5, white);
    spr.fillRect(x + size / 2 + size / 20, y + size / 4, size / 5, size / 5,
                 white);
    spr.fillRect(x + size / 4, y + size / 2 + size / 20, size / 5, size / 5,
                 white);
    spr.fillRect(x + size / 2 + size / 20, y + size / 2 + size / 20, size / 5,
                 size / 5, white);
    break;
  case 2: // Prism
    spr.fillTriangle(cx, y + size / 4, x + size / 4, y + size * 3 / 4,
                     x + size * 3 / 4, y + size * 3 / 4, white);
    spr.drawLine(x, cy, cx, y + size / 4, white);
    break;
  case 3: // Stripes
    spr.fillRect(x + size * 2 / 10, y + size * 2 / 10, size * 15 / 100,
                 size * 6 / 10, white);
    spr.fillRect(x + size * 45 / 100, y + size * 2 / 10, size * 15 / 100,
                 size * 6 / 10, black);
    spr.fillRect(x + size * 7 / 10, y + size * 2 / 10, size * 15 / 100,
                 size * 6 / 10, white);
    break;
  case 4: // Waves
    for (int i = 0; i < 5; i++) {
      int yy = y + size / 5 + (i * size / 7);
      int y2 = yy + (i % 2 == 0 ? size / 14 : -size / 14);
      spr.drawWideLine(x + size / 6, yy, x + size * 5 / 6, y2,
                       max(1, 3 * size / CENTER_SIZE), white);
    }
    break;
  case 5: // Blocks
    spr.fillRoundRect(x + size * 2 / 10, y + size * 2 / 10, size * 6 / 10,
                      size * 3 / 10, max(1, 4 * size / CENTER_SIZE), white);
    spr.fillRoundRect(x + size * 5 / 10, y + size * 6 / 10, size * 3 / 10,
                      size * 2 / 10, max(1, 4 * size / CENTER_SIZE), black);
    spr.fillRoundRect(x + size * 2 / 10, y + size * 6 / 10, size * 2 / 10,
                      size * 2 / 10, max(1, 4 * size / CENTER_SIZE), black);
    break;
  }
}

// Get Cover Flow position for an album given its offset from center (in
// fixed-point /100) Returns: size, x_center via pointers, and a "depth" level
// (0=center, 1=side, 2=far, 3=offscreen)
static int getCoverFlowPos(int32_t offset_fp, int *out_size, int *out_cx) {
  // offset_fp is in units of SCROLL_SCALE (100 = one album away)
  int32_t absOff = abs(offset_fp);
  int sign = (offset_fp < 0) ? -1 : ((offset_fp > 0) ? 1 : 0);

  int size, cx_offset;
  int depth;

  if (absOff <= SCROLL_SCALE) {
    // Between center and side: interpolate 0..100%
    size = iLerp(CENTER_SIZE, SIDE_SIZE, absOff);
    cx_offset = sign * iLerp(0, SIDE_OFFSET, absOff);
    depth = (absOff < 30) ? 0 : 1;
  } else if (absOff <= 2 * SCROLL_SCALE) {
    // Between side and far
    int32_t t = absOff - SCROLL_SCALE;
    size = iLerp(SIDE_SIZE, FAR_SIZE, t);
    cx_offset = sign * iLerp(SIDE_OFFSET, FAR_OFFSET, t);
    depth = 2;
  } else {
    // Off screen
    *out_size = 0;
    *out_cx = 0;
    return 3;
  }

  *out_size = size;
  *out_cx = SCREEN_W / 2 + cx_offset;
  return depth;
}

static void draw_ui() {
  if (!sprite_created) {
    spr.setColorDepth(16);
    if (spr.createSprite(SCREEN_W, SPRITE_H) == nullptr) {
      Serial.println("SPRITE ALLOCATION FAILED");
      return;
    }
    sprite_created = true;
  }

  spr.fillSprite(TFT_BLACK);

  // Compute transforms for all albums
  struct AlbumDraw {
    int index;
    int size;
    int cx;
    int depth;
    int absOffset;
  };
  AlbumDraw items[ALBUM_COUNT];
  int itemCount = 0;

  for (int i = 0; i < ALBUM_COUNT; i++) {
    int32_t offset_fp = (int32_t)i * SCROLL_SCALE - scroll_pos;
    int size, cx;
    int depth = getCoverFlowPos(offset_fp, &size, &cx);

    if (depth < 3 && size > 10) {
      items[itemCount].index = i;
      items[itemCount].size = size;
      items[itemCount].cx = cx;
      items[itemCount].depth = depth;
      items[itemCount].absOffset = abs(offset_fp);
      itemCount++;
    }
  }

  // Sort by depth descending (draw far albums first = painter's algorithm)
  // Simple bubble sort — max 5 items, negligible cost
  for (int i = 0; i < itemCount - 1; i++) {
    for (int j = 0; j < itemCount - 1 - i; j++) {
      if (items[j].absOffset < items[j + 1].absOffset) {
        AlbumDraw tmp = items[j];
        items[j] = items[j + 1];
        items[j + 1] = tmp;
      }
    }
  }

  // Draw albums into sprite (sprite is SPRITE_H tall, albums are vertically
  // centered)
  for (int n = 0; n < itemCount; n++) {
    int i = items[n].index;
    int size = items[n].size;
    int cx = items[n].cx;
    int depth = items[n].depth;

    int x = cx - size / 2;
    int y = (SPRITE_H - size) / 2; // Center vertically in sprite

    // Pick color based on depth
    uint16_t color;
    if (depth == 0)
      color = album_colors[i];
    else if (depth == 1)
      color = album_colors_dim[i];
    else
      color = album_colors_far[i];

    // Clip to sprite bounds
    if (x + size > 0 && x < SCREEN_W) {
      drawAlbumArtPlaceholder(x, y, size, color, i);
    }
  }

  // Push sprite to screen
  int y_offset = (SCREEN_H - SPRITE_H) / 2;
  spr.pushSprite(0, y_offset);

  // Draw center album name below sprite, directly to TFT
  tft.fillRect(0, y_offset + SPRITE_H, SCREEN_W, 30, TFT_BLACK);

  int centerIndex = (scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE;
  if (centerIndex < 0)
    centerIndex = 0;
  if (centerIndex >= ALBUM_COUNT)
    centerIndex = ALBUM_COUNT - 1;

  // Only show name when close to centered (within 40% of one album)
  int32_t snapDist = abs(scroll_pos - (int32_t)centerIndex * SCROLL_SCALE);
  if (snapDist < 40) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(album_names[centerIndex], SCREEN_W / 2,
                   y_offset + SPRITE_H + 6, 2);
  }

  // Clear areas above sprite
  tft.fillRect(0, 0, SCREEN_W, y_offset, TFT_BLACK);
}

void ui_init() {
  scroll_pos = 0;
  target_scroll = 0;
  draw_ui();
}

void ui_update() {
  static int32_t last_scroll_pos = -1;
  int16_t tx, ty;
  bool touched = get_touch_coords(&tx, &ty);

  if (touched) {
    if (!is_dragging) {
      is_dragging = true;
      touch_start_x = tx;
      last_tx = tx;
      scroll_start = scroll_pos;
      touch_start_time = millis();
      momentum_velocity = 0;
    } else {
      // Actively Dragging
      int16_t dx = tx - touch_start_x;
      scroll_pos = scroll_start - (int32_t)dx * SCROLL_SCALE / DRAG_SENSITIVITY;

      // Flick speed (in fixed-point)
      momentum_velocity =
          ((int32_t)(last_tx - tx) * SCROLL_SCALE) / DRAG_SENSITIVITY;
      last_tx = tx;

      // Soft boundaries (overscroll by ~0.6 albums)
      int32_t min_scroll = -60; // -0.6 * SCROLL_SCALE
      int32_t max_s = (int32_t)(ALBUM_COUNT - 1) * SCROLL_SCALE + 60;
      if (scroll_pos < min_scroll)
        scroll_pos = min_scroll;
      if (scroll_pos > max_s)
        scroll_pos = max_s;

      target_scroll = scroll_pos;
    }
  } else {
    if (is_dragging) {
      is_dragging = false;

      unsigned long duration = millis() - touch_start_time;
      int32_t totalDrag = abs(scroll_pos - scroll_start);

      // Tap detection (moved less than 8% of one album and under 300ms)
      if (totalDrag < 8 && duration < 300) {
        int centerIndex = (scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE;
        if (centerIndex < 0)
          centerIndex = 0;
        if (centerIndex >= ALBUM_COUNT)
          centerIndex = ALBUM_COUNT - 1;
        Serial.print("Tapped Album: ");
        Serial.println(album_names[centerIndex]);
        spotify_play_album(album_uris[centerIndex]);
      } else {
        // Flick: apply momentum
        target_scroll = scroll_pos + momentum_velocity * 35 / 10;
      }

      // Hard boundaries
      if (target_scroll < 0)
        target_scroll = 0;
      if (target_scroll > (int32_t)(ALBUM_COUNT - 1) * SCROLL_SCALE)
        target_scroll = (int32_t)(ALBUM_COUNT - 1) * SCROLL_SCALE;

      // Snap to nearest album
      int closest = (target_scroll + SCROLL_SCALE / 2) / SCROLL_SCALE;
      if (closest < 0)
        closest = 0;
      if (closest >= ALBUM_COUNT)
        closest = ALBUM_COUNT - 1;
      target_scroll = (int32_t)closest * SCROLL_SCALE;
    }
  }

  // Smooth easing towards target
  if (!is_dragging && scroll_pos != target_scroll) {
    int32_t diff = target_scroll - scroll_pos;
    scroll_pos += diff / 5;

    if (abs(diff) < 2) {
      scroll_pos = target_scroll;
    }
  }

  // Cap framerate to ~60FPS
  static unsigned long last_frame_time = 0;
  if (millis() - last_frame_time >= 16) {
    if (scroll_pos != last_scroll_pos) {
      draw_ui();
      last_scroll_pos = scroll_pos;
      last_frame_time = millis();
    }
  }
}

void ui_show_album_browser() {}
void ui_show_now_playing() {}