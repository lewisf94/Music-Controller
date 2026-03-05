#include "ui.h"
#include "spotify.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;
extern bool get_touch_coords(int16_t *x, int16_t *y);

// Sprite for double buffering
TFT_eSprite spr = TFT_eSprite(&tft);
static bool sprite_created = false;

#define ALBUM_COUNT 6
#define SCREEN_W 320
#define SCREEN_H 240

// Cover Flow layout — albums rotate away from center
#define CENTER_SIZE 120 // Full-size center album (height & width)
#define SIDE_SIZE 100   // Side album height (width = height * scaleX)
#define FAR_SIZE 80     // Far album height
#define SIDE_OFFSET 95  // X position offset for ±1 albums
#define FAR_OFFSET 145  // X position offset for ±2 albums
#define SPRITE_H 140    // Sprite buffer height (320*140*2 = 89.6KB, within RAM)

// Colors
static uint16_t album_colors[ALBUM_COUNT] = {
    tft.color565(231, 76, 60),  tft.color565(52, 152, 219),
    tft.color565(46, 204, 113), tft.color565(243, 156, 18),
    tft.color565(155, 89, 182), tft.color565(26, 188, 156),
};

// Pre-computed dimmed colors for side albums (~55% brightness)
static uint16_t album_colors_dim[ALBUM_COUNT] = {
    tft.color565(127, 42, 33), tft.color565(29, 84, 120),
    tft.color565(25, 112, 62), tft.color565(134, 86, 10),
    tft.color565(85, 49, 100), tft.color565(14, 103, 86),
};

// Pre-computed far colors (~30% brightness)
static uint16_t album_colors_far[ALBUM_COUNT] = {
    tft.color565(69, 23, 18), tft.color565(16, 46, 66),
    tft.color565(14, 61, 34), tft.color565(73, 47, 5),
    tft.color565(47, 27, 55), tft.color565(8, 56, 47),
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

// --- Scroll State (fixed-point, x100) ---
static int32_t scroll_pos = 0;
static int32_t target_scroll = 0;
#define SCROLL_SCALE 100

// --- Fluid Scroll Tuning ---
#define DRAG_SENSITIVITY 180 // Pixels per album (higher = less twitchy)
#define MOMENTUM_MULT 35     // /10 = 3.5x momentum (subtle glide)
#define EASE_DIVISOR 6       // Easing speed
#define OVERSCROLL_LIMIT 30  // 0.3 * SCROLL_SCALE

// --- Touch State ---
static bool is_dragging = false;
static int16_t touch_start_x = 0;
static int32_t scroll_start = 0;
static unsigned long touch_start_time = 0;
static int32_t momentum_velocity = 0;
static int16_t last_tx = 0;

static int32_t iLerp(int32_t a, int32_t b, int32_t t) {
  if (t <= 0)
    return a;
  if (t >= 100)
    return b;
  return a + ((b - a) * t) / 100;
}

// Draw album art at given position and width (width < height = rotated look)
static void drawAlbumArt(int x, int y, int w, int h, uint16_t color,
                         int index) {
  if (w < 6 || h < 6)
    return;

  int r = max(2, 12 * w / CENTER_SIZE);
  spr.fillRoundRect(x, y, w, h, r, color);

  // Skip detail on very small albums
  if (w < 25)
    return;

  uint16_t white = TFT_WHITE;
  uint16_t black = TFT_BLACK;
  int cx = x + w / 2;
  int cy = y + h / 2;

  switch (index % 6) {
  case 0: // Vinyl — ellipse to show rotation
    spr.fillEllipse(cx, cy, w / 3, h / 3, black);
    spr.fillEllipse(cx, cy, w / 10, h / 10, color);
    break;
  case 1: // Window
    spr.fillRect(x + w * 20 / 100, y + h * 25 / 100, w * 22 / 100, h * 20 / 100,
                 white);
    spr.fillRect(x + w * 55 / 100, y + h * 25 / 100, w * 22 / 100, h * 20 / 100,
                 white);
    spr.fillRect(x + w * 20 / 100, y + h * 55 / 100, w * 22 / 100, h * 20 / 100,
                 white);
    spr.fillRect(x + w * 55 / 100, y + h * 55 / 100, w * 22 / 100, h * 20 / 100,
                 white);
    break;
  case 2: // Prism
    spr.fillTriangle(cx, y + h / 4, x + w / 4, y + h * 3 / 4, x + w * 3 / 4,
                     y + h * 3 / 4, white);
    spr.drawLine(x, cy, cx, y + h / 4, white);
    break;
  case 3: // Stripes
    spr.fillRect(x + w * 18 / 100, y + h * 20 / 100, w * 16 / 100, h * 60 / 100,
                 white);
    spr.fillRect(x + w * 43 / 100, y + h * 20 / 100, w * 16 / 100, h * 60 / 100,
                 black);
    spr.fillRect(x + w * 68 / 100, y + h * 20 / 100, w * 16 / 100, h * 60 / 100,
                 white);
    break;
  case 4: // Waves
    for (int i = 0; i < 5; i++) {
      int yy = y + h * 18 / 100 + i * h * 14 / 100;
      int y2 = yy + (i % 2 == 0 ? h * 5 / 100 : -h * 5 / 100);
      spr.drawWideLine(x + w * 15 / 100, yy, x + w * 85 / 100, y2,
                       max(1, 2 * w / CENTER_SIZE), white);
    }
    break;
  case 5: // Blocks
    spr.fillRoundRect(x + w * 18 / 100, y + h * 20 / 100, w * 64 / 100,
                      h * 28 / 100, max(1, 3 * w / CENTER_SIZE), white);
    spr.fillRoundRect(x + w * 50 / 100, y + h * 58 / 100, w * 30 / 100,
                      h * 20 / 100, max(1, 3 * w / CENTER_SIZE), black);
    spr.fillRoundRect(x + w * 18 / 100, y + h * 58 / 100, w * 24 / 100,
                      h * 20 / 100, max(1, 3 * w / CENTER_SIZE), black);
    break;
  }
}

// Compute Cover Flow position for an album at given offset (fixed-point /100)
// Stores width, height, cx, and returns depth tier (0=center, 1=side, 2=far,
// 3=offscreen)
static int getCoverFlowPos(int32_t offset_fp, int *out_w, int *out_h,
                           int *out_cx) {
  int32_t absOff = abs(offset_fp);
  int sign = (offset_fp < 0) ? -1 : ((offset_fp > 0) ? 1 : 0);

  int height, width, cx_offset;

  if (absOff <= SCROLL_SCALE) {
    // Center → side: height stays close, width squishes to simulate rotation
    height = iLerp(CENTER_SIZE, SIDE_SIZE, absOff);
    width =
        iLerp(CENTER_SIZE, SIDE_SIZE * 55 / 100, absOff); // scaleX: 1.0 → 0.55
    // Non-linear offset for natural spread
    int32_t t_curve = absOff * 85 / 100;
    cx_offset = sign * iLerp(0, SIDE_OFFSET, t_curve > 100 ? 100 : t_curve);
    *out_w = width;
    *out_h = height;
    *out_cx = SCREEN_W / 2 + cx_offset;
    return (absOff < 30) ? 0 : 1;
  } else if (absOff <= 2 * SCROLL_SCALE) {
    int32_t t = absOff - SCROLL_SCALE;
    height = iLerp(SIDE_SIZE, FAR_SIZE, t);
    width = iLerp(SIDE_SIZE * 55 / 100, FAR_SIZE * 40 / 100,
                  t); // scaleX: 0.55 → 0.40
    cx_offset = sign * iLerp(SIDE_OFFSET, FAR_OFFSET, t);
    *out_w = width;
    *out_h = height;
    *out_cx = SCREEN_W / 2 + cx_offset;
    return 2;
  }

  *out_w = 0;
  *out_h = 0;
  *out_cx = 0;
  return 3;
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

  // Collect visible albums with transforms
  struct AlbumDraw {
    int index, w, h, cx, depth;
    int32_t absOffset;
  };
  AlbumDraw items[ALBUM_COUNT];
  int itemCount = 0;

  for (int i = 0; i < ALBUM_COUNT; i++) {
    int32_t offset_fp = (int32_t)i * SCROLL_SCALE - scroll_pos;
    int w, h, cx;
    int depth = getCoverFlowPos(offset_fp, &w, &h, &cx);

    if (depth < 3 && w > 5) {
      items[itemCount] = {i, w, h, cx, depth, abs(offset_fp)};
      itemCount++;
    }
  }

  // Sort: draw far albums first (painter's algorithm)
  for (int i = 0; i < itemCount - 1; i++) {
    for (int j = 0; j < itemCount - 1 - i; j++) {
      if (items[j].absOffset < items[j + 1].absOffset) {
        AlbumDraw tmp = items[j];
        items[j] = items[j + 1];
        items[j + 1] = tmp;
      }
    }
  }

  // Draw into sprite
  for (int n = 0; n < itemCount; n++) {
    int i = items[n].index;
    int w = items[n].w;
    int h = items[n].h;
    int x = items[n].cx - w / 2;
    int y = (SPRITE_H - h) / 2;

    uint16_t color;
    if (items[n].depth == 0)
      color = album_colors[i];
    else if (items[n].depth == 1)
      color = album_colors_dim[i];
    else
      color = album_colors_far[i];

    if (x + w > 0 && x < SCREEN_W) {
      drawAlbumArt(x, y, w, h, color, i);
    }
  }

  // Push sprite
  int y_offset = (SCREEN_H - SPRITE_H) / 2;
  spr.pushSprite(0, y_offset);

  // Album name (center only)
  tft.fillRect(0, y_offset + SPRITE_H, SCREEN_W, 30, TFT_BLACK);
  int centerIndex = (scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE;
  centerIndex = constrain(centerIndex, 0, ALBUM_COUNT - 1);

  int32_t snapDist = abs(scroll_pos - (int32_t)centerIndex * SCROLL_SCALE);
  if (snapDist < 35) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(album_names[centerIndex], SCREEN_W / 2,
                   y_offset + SPRITE_H + 6, 2);
  }

  // Clear top
  tft.fillRect(0, 0, SCREEN_W, y_offset, TFT_BLACK);
}

void ui_init() {
  scroll_pos = 0;
  target_scroll = 0;
  draw_ui();
}

void ui_update() {
  static int32_t last_scroll_pos = -1;
  static unsigned long last_update_time = 0;
  int16_t tx, ty;
  bool touched = get_touch_coords(&tx, &ty);

  unsigned long now = millis();
  unsigned long dt = now - last_update_time;
  if (dt > 100)
    dt = 16; // Clamp after pauses
  last_update_time = now;

  if (touched) {
    if (!is_dragging) {
      is_dragging = true;
      touch_start_x = tx;
      last_tx = tx;
      scroll_start = scroll_pos;
      touch_start_time = millis();
      momentum_velocity = 0;
    } else {
      int16_t dx = tx - touch_start_x;
      scroll_pos = scroll_start - (int32_t)dx * SCROLL_SCALE / DRAG_SENSITIVITY;

      // Smooth momentum: blend old and new velocity (50/50) to reduce spikes
      int32_t new_vel =
          ((int32_t)(last_tx - tx) * SCROLL_SCALE) / DRAG_SENSITIVITY;
      momentum_velocity = (momentum_velocity + new_vel) / 2;
      last_tx = tx;

      int32_t lo = -OVERSCROLL_LIMIT;
      int32_t hi = (int32_t)(ALBUM_COUNT - 1) * SCROLL_SCALE + OVERSCROLL_LIMIT;
      scroll_pos = constrain(scroll_pos, lo, hi);
      target_scroll = scroll_pos;
    }
  } else {
    if (is_dragging) {
      is_dragging = false;
      unsigned long duration = millis() - touch_start_time;
      int32_t totalDrag = abs(scroll_pos - scroll_start);

      if (totalDrag < 6 && duration < 300) {
        // Tap — play center album
        int ci = constrain((scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE, 0,
                           ALBUM_COUNT - 1);
        Serial.print("Tapped: ");
        Serial.println(album_names[ci]);
        spotify_play_album(album_uris[ci]);
        target_scroll = (int32_t)ci * SCROLL_SCALE;
      } else {
        // Flick with momentum
        target_scroll = scroll_pos + momentum_velocity * MOMENTUM_MULT / 10;
      }

      target_scroll = constrain(target_scroll, (int32_t)0,
                                (int32_t)(ALBUM_COUNT - 1) * SCROLL_SCALE);
      int closest = constrain((target_scroll + SCROLL_SCALE / 2) / SCROLL_SCALE,
                              0, ALBUM_COUNT - 1);
      target_scroll = (int32_t)closest * SCROLL_SCALE;
    }
  }

  // Delta-time exponential easing (frame-rate independent, smooth)
  if (!is_dragging && scroll_pos != target_scroll) {
    // lerp factor: ~15% per 16ms frame, scales with actual dt
    int32_t diff = target_scroll - scroll_pos;
    int32_t step = diff * (int32_t)dt / 100;
    if (step == 0)
      step = (diff > 0) ? 1 : -1; // Always make progress
    if (abs(diff) <= 2)
      scroll_pos = target_scroll;
    else
      scroll_pos += step;
  }

  // ~60fps cap
  static unsigned long last_frame = 0;
  if (millis() - last_frame >= 16) {
    if (scroll_pos != last_scroll_pos) {
      draw_ui();
      last_scroll_pos = scroll_pos;
      last_frame = millis();
    }
  }
}

void ui_show_album_browser() {}
void ui_show_now_playing() {}