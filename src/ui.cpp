#include "ui.h"
#include "spotify.h"
#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;
extern bool get_touch_coords(int16_t *x, int16_t *y);

// Sprite for double buffering
TFT_eSprite spr = TFT_eSprite(&tft);
static bool sprite_created = false;

// --- Album Capacity ---
#define MAX_ALBUMS 100
static int album_count = 0; // Actual number loaded from SD

#define SCREEN_W 320
#define SCREEN_H 240

// Simple horizontal slide layout — all albums same size
#define ALBUM_SIZE 80
#define ALBUM_SPACING 100  // Center-to-center distance between albums
#define SPRITE_H 100
#define IMG_SRC_SIZE 80
#define IMG_PIXELS (IMG_SRC_SIZE * IMG_SRC_SIZE)

// --- Per-Album Metadata (loaded from SD metadata.csv) ---
static char album_filenames[MAX_ALBUMS][128];
static char album_titles[MAX_ALBUMS][32];
static char album_artists[MAX_ALBUMS][24];
static char album_uris[MAX_ALBUMS][48];

// --- 3-Slot SD Image Cache (LRU, heap-allocated) ---
extern bool sd_ok;
#define CACHE_SLOTS 3
static uint16_t *sd_img_cache[CACHE_SLOTS] = {nullptr, nullptr, nullptr};
static int cache_album_idx[CACHE_SLOTS] = {-1, -1, -1};
static unsigned long cache_access_time[CACHE_SLOTS] = {0, 0, 0};

static uint16_t fallback_color = 0x4208;

static void initCache() {
  for (int s = 0; s < CACHE_SLOTS; s++) {
    if (!sd_img_cache[s]) {
      if (ESP.getFreeHeap() < 20000) {
        Serial.print("Not enough heap for cache slot ");
        Serial.println(s);
        break;
      }
      sd_img_cache[s] = (uint16_t *)malloc(IMG_PIXELS * 2);
      if (sd_img_cache[s]) {
        Serial.print("Cache slot ");
        Serial.print(s);
        Serial.println(" OK");
      } else {
        Serial.print("Cache slot ");
        Serial.print(s);
        Serial.println(" malloc failed");
      }
    }
  }
}

// --- Scroll State (fixed-point, x100) ---
// ============================================================
// TUNING — Change these values to adjust feel
// ============================================================

// --- Encoder tuning ---
// Easing speed when scrolling via encoder.
// Lower = faster snap. 5 = near-instant, 12 = smooth, 20 = slow.
#define ENCODER_EASE_SPEED 5
// Min step per frame for encoder (prevents slow crawl at end).
#define ENCODER_MIN_STEP 8

// --- Touch tuning ---
// Easing speed when snapping after touch release.
// Lower = faster snap. 5 = near-instant, 12 = smooth, 20 = slow.
#define TOUCH_EASE_SPEED 10
// Min step per frame for touch snap.
#define TOUCH_MIN_STEP 5
// How many pixels of finger movement = 1 album scroll.
// Lower = more sensitive. 30 = very fast, 50 = moderate, 100 = slow.
#define TOUCH_DRAG_DIVISOR 40
// How far you can scroll past the first/last album.
#define OVERSCROLL_LIMIT 30

// --- Scroll State (fixed-point, x100) ---
static int32_t scroll_pos = 0;
static int32_t target_scroll = 0;
#define SCROLL_SCALE 100

// --- Touch State ---
static bool is_dragging = false;
static int16_t touch_start_x = 0;
static int32_t scroll_start = 0;
static unsigned long touch_start_time = 0;
static int32_t momentum_velocity = 0;
static bool ease_from_encoder = false;  // Which input triggered current easing

// ============================================================
// SD Card Album Loading
// ============================================================

// Parse one CSV line into fields (handles quoted fields with commas)
static int parseCsvLine(char *line, char *fields[], int maxFields) {
  int count = 0;
  char *p = line;
  while (*p && count < maxFields) {
    if (*p == '"') {
      p++;
      fields[count] = p;
      while (*p && !(*p == '"' && (*(p + 1) == ',' || *(p + 1) == '\0' ||
                                   *(p + 1) == '\r' || *(p + 1) == '\n')))
        p++;
      if (*p == '"')
        *p++ = '\0';
      if (*p == ',')
        p++;
    } else {
      fields[count] = p;
      while (*p && *p != ',' && *p != '\r' && *p != '\n')
        p++;
      if (*p == ',')
        *p++ = '\0';
      else if (*p)
        *p++ = '\0';
    }
    count++;
  }
  return count;
}

static void loadAlbumsFromSD() {
  if (!sd_ok) {
    Serial.println("SD not available, no albums loaded");
    return;
  }

  Serial.println("SD card mounted. Scanning /sd_card_albums/...");

  File dir = SD.open("/sd_card_albums");
  if (!dir) {
    Serial.println("ERROR: /sd_card_albums folder not found!");
    return;
  }
  int fileCount = 0;
  while (File entry = dir.openNextFile()) {
    Serial.print("  Found: ");
    Serial.println(entry.name());
    fileCount++;
    entry.close();
  }
  dir.close();
  Serial.print("Total files in folder: ");
  Serial.println(fileCount);

  File f = SD.open("/sd_card_albums/metadata.csv", FILE_READ);
  if (!f) {
    Serial.println("ERROR: metadata.csv not found!");
    return;
  }

  char lineBuf[512];
  album_count = 0;

  while (f.available() && album_count < MAX_ALBUMS) {
    int len = 0;
    while (f.available() && len < 511) {
      char c = f.read();
      if (c == '\n')
        break;
      if (c != '\r')
        lineBuf[len++] = c;
    }
    lineBuf[len] = '\0';
    if (len == 0)
      continue;

    char *fields[4] = {nullptr, nullptr, nullptr, nullptr};
    int fieldCount = parseCsvLine(lineBuf, fields, 4);
    if (fieldCount < 3)
      continue;

    int i = album_count;
    strncpy(album_filenames[i], fields[0], 127);
    album_filenames[i][127] = '\0';
    strncpy(album_titles[i], fields[1], 31);
    album_titles[i][31] = '\0';
    strncpy(album_artists[i], fields[2], 23);
    album_artists[i][23] = '\0';
    if (fieldCount >= 4 && fields[3]) {
      strncpy(album_uris[i], fields[3], 47);
      album_uris[i][47] = '\0';
    } else {
      album_uris[i][0] = '\0';
    }

    // Verify this file actually exists on SD
    char path[128];
    snprintf(path, sizeof(path), "/sd_card_albums/%s", album_filenames[i]);
    File test = SD.open(path, FILE_READ);
    if (test) {
      test.close();
      album_count++;
    } else {
      Serial.print("SKIP (file not found): ");
      Serial.println(path);
    }
  }
  f.close();

  Serial.print("Loaded ");
  Serial.print(album_count);
  Serial.println(" albums from SD card");
}

// ============================================================
// Image Loading & Drawing
// ============================================================

// Find a cache slot for an album (returns slot index, or -1 on failure)
static int loadAlbumImage(int index) {
  if (!sd_ok || index < 0 || index >= album_count)
    return -1;

  // Check if already cached
  for (int s = 0; s < CACHE_SLOTS; s++) {
    if (cache_album_idx[s] == index && sd_img_cache[s]) {
      cache_access_time[s] = millis();
      return s;
    }
  }

  // Find LRU slot
  int lru = -1;
  for (int s = 0; s < CACHE_SLOTS; s++) {
    if (!sd_img_cache[s])
      continue;
    if (lru == -1 || cache_access_time[s] < cache_access_time[lru])
      lru = s;
  }
  if (lru == -1)
    return -1;

  char path[128];
  snprintf(path, sizeof(path), "/sd_card_albums/%s", album_filenames[index]);

  File f = SD.open(path, FILE_READ);
  if (!f)
    return -1;

  size_t bytesRead = f.read((uint8_t *)sd_img_cache[lru], IMG_PIXELS * 2);
  f.close();

  if (bytesRead != (size_t)(IMG_PIXELS * 2))
    return -1;

  for (int i = 0; i < IMG_PIXELS; i++) {
    uint16_t v = sd_img_cache[lru][i];
    sd_img_cache[lru][i] = (v >> 8) | (v << 8);
  }

  cache_album_idx[lru] = index;
  cache_access_time[lru] = millis();
  return lru;
}

static void drawAlbumArt(int x, int y, int index) {
  int slot = loadAlbumImage(index);
  if (slot >= 0) {
    spr.setSwapBytes(true);
    spr.pushImage(x, y, ALBUM_SIZE, ALBUM_SIZE, sd_img_cache[slot]);
    spr.setSwapBytes(false);
  } else {
    spr.fillRoundRect(x, y, ALBUM_SIZE, ALBUM_SIZE, 4, fallback_color);
  }
}

// ============================================================
// Drawing
// ============================================================

static void draw_ui() {
  if (album_count == 0) {
    // Show error message instead of blank screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("No albums found", SCREEN_W / 2, SCREEN_H / 2 - 20, 2);
    tft.setTextColor(tft.color565(160, 160, 160));
    tft.setTextDatum(MC_DATUM);
    if (!sd_ok) {
      tft.drawString("SD card not detected", SCREEN_W / 2, SCREEN_H / 2 + 10,
                     1);
    } else {
      tft.drawString("Copy metadata.csv to SD card", SCREEN_W / 2,
                     SCREEN_H / 2 + 10, 1);
    }
    return;
  }

  if (!sprite_created)
    return; // Sprite created in ui_init

  spr.fillSprite(TFT_BLACK);

  // Simple horizontal slide: calculate pixel offset from scroll position
  // scroll_pos is in fixed-point (x100), so album i is at pixel center:
  //   cx = SCREEN_W/2 + (i * ALBUM_SPACING) - scroll_pos * ALBUM_SPACING / SCROLL_SCALE
  int32_t scroll_px = scroll_pos * ALBUM_SPACING / SCROLL_SCALE;
  int album_y = (SPRITE_H - ALBUM_SIZE) / 2;

  for (int i = 0; i < album_count; i++) {
    int cx = SCREEN_W / 2 + i * ALBUM_SPACING - scroll_px;
    int ax = cx - ALBUM_SIZE / 2;

    // Skip if off-screen
    if (ax + ALBUM_SIZE < 0 || ax >= SCREEN_W)
      continue;

    drawAlbumArt(ax, album_y, i);
  }

  // Push sprite
  int y_offset = (SCREEN_H - SPRITE_H) / 2;
  spr.pushSprite(0, y_offset);

  // Album title + artist text (center only)
  tft.fillRect(0, y_offset + SPRITE_H, SCREEN_W, 30, TFT_BLACK);
  int centerIndex = (scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE;
  centerIndex = constrain(centerIndex, 0, album_count - 1);

  int32_t snapDist = abs(scroll_pos - (int32_t)centerIndex * SCROLL_SCALE);
  if (snapDist < 35) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(album_titles[centerIndex], SCREEN_W / 2,
                   y_offset + SPRITE_H + 4, 2);
    tft.setTextColor(tft.color565(160, 160, 160));
    tft.drawString(album_artists[centerIndex], SCREEN_W / 2,
                   y_offset + SPRITE_H + 20, 1);
  }

  // Clear top
  tft.fillRect(0, 0, SCREEN_W, y_offset, TFT_BLACK);
}

// ============================================================
// Public API
// ============================================================

void ui_init() {
  // CRITICAL: Create sprite FIRST — it's the largest allocation (89.6KB)
  // and must succeed for any display to work
  if (!sprite_created) {
    spr.setColorDepth(16);
    if (spr.createSprite(SCREEN_W, SPRITE_H) == nullptr) {
      Serial.println("SPRITE ALLOCATION FAILED");
    } else {
      sprite_created = true;
      Serial.println("Sprite created OK");
    }
  }

  // Now allocate cache slots from remaining heap
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  initCache();

  loadAlbumsFromSD();
  scroll_pos = 0;
  target_scroll = 0;
  draw_ui();
}

void ui_update() {
  if (album_count == 0)
    return;

  static int32_t last_scroll_pos = -1;
  static unsigned long last_update_time = 0;
  unsigned long now = millis();
  unsigned long dt = now - last_update_time;
  if (dt > 100) dt = 16;
  if (dt > 0) last_update_time = now;

  int16_t tx, ty;
  bool touched = get_touch_coords(&tx, &ty);

  // --- Rotary encoder: sets target, cancels touch ---
  extern int32_t get_encoder_delta();
  int32_t enc = -get_encoder_delta();
  if (enc > 1) enc = 1;
  if (enc < -1) enc = -1;
  bool encoder_active = false;
  if (enc != 0) {
    encoder_active = true;
    is_dragging = false;
    momentum_velocity = 0;

    int current_album = constrain(
        (target_scroll + SCROLL_SCALE / 2) / SCROLL_SCALE, 0, album_count - 1);
    int next_album = constrain(current_album + enc, 0, album_count - 1);
    target_scroll = (int32_t)next_album * SCROLL_SCALE;
    ease_from_encoder = true;
  }

  // --- Touch input (only if encoder didn't fire this frame) ---
  if (!encoder_active) {
    if (touched) {
      if (!is_dragging) {
        is_dragging = true;
        touch_start_x = tx;
        scroll_start = scroll_pos;
        touch_start_time = now;
        momentum_velocity = 0;
      } else {
        int16_t dx = tx - touch_start_x;
        scroll_pos = scroll_start - (int32_t)dx * SCROLL_SCALE / TOUCH_DRAG_DIVISOR;

        int32_t lo = -OVERSCROLL_LIMIT;
        int32_t hi = (int32_t)(album_count - 1) * SCROLL_SCALE + OVERSCROLL_LIMIT;
        scroll_pos = constrain(scroll_pos, lo, hi);
        target_scroll = scroll_pos;
      }
    } else if (is_dragging) {
      // Touch released — snap to nearest album (no momentum fling)
      is_dragging = false;
      unsigned long duration = now - touch_start_time;
      int32_t totalDrag = abs(scroll_pos - scroll_start);

      if (totalDrag < 6 && duration < 300) {
        // Tap — play center album
        int ci = constrain((scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE, 0,
                           album_count - 1);
        Serial.print("Tapped: ");
        Serial.println(album_titles[ci]);
        if (album_uris[ci][0] != '\0') {
          spotify_play_album(album_uris[ci]);
        }
      }

      // Snap to nearest album
      int closest = constrain((scroll_pos + SCROLL_SCALE / 2) / SCROLL_SCALE,
                              0, album_count - 1);
      target_scroll = (int32_t)closest * SCROLL_SCALE;
      ease_from_encoder = false;
    }
  }

  // --- Easing toward target (uses encoder or touch speed) ---
  if (dt > 0 && !is_dragging && scroll_pos != target_scroll) {
    int32_t ease_spd = ease_from_encoder ? ENCODER_EASE_SPEED : TOUCH_EASE_SPEED;
    int32_t min_stp = ease_from_encoder ? ENCODER_MIN_STEP : TOUCH_MIN_STEP;
    int32_t diff = target_scroll - scroll_pos;
    int32_t step = diff * (int32_t)dt / ease_spd;
    if (step == 0 || (abs(step) < min_stp && abs(diff) > 1))
      step = (diff > 0) ? min_stp : -min_stp;

    if (abs(diff) <= abs(step))
      scroll_pos = target_scroll;
    else
      scroll_pos += step;
  }

  // --- Draw at ~60fps ---
  static unsigned long last_frame = 0;
  if (now - last_frame >= 16) {
    if (scroll_pos != last_scroll_pos) {
      draw_ui();
      last_scroll_pos = scroll_pos;
    }
    last_frame = now;
  }
}

void ui_show_album_browser() {}
void ui_show_now_playing() {}