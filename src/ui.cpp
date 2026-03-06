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

// Cover Flow layout — albums rotate away from center
#define CENTER_SIZE 120
#define SIDE_SIZE 100
#define FAR_SIZE 80
#define SIDE_OFFSET 95
#define FAR_OFFSET 145
#define SPRITE_H 140
#define IMG_SRC_SIZE 120
#define IMG_PIXELS (IMG_SRC_SIZE * IMG_SRC_SIZE)

// --- Per-Album Metadata (loaded from SD metadata.csv) ---
static char album_filenames[MAX_ALBUMS][64];
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
      // Check if enough heap remains (need 28.8KB + margin)
      if (ESP.getFreeHeap() < 40000) {
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
static int32_t scroll_pos = 0;
static int32_t target_scroll = 0;
#define SCROLL_SCALE 100

// --- Fluid Scroll Tuning ---
#define DRAG_SENSITIVITY 180
#define MOMENTUM_MULT 35
#define EASE_DIVISOR 6
#define OVERSCROLL_LIMIT 30

// --- Touch State ---
static bool is_dragging = false;
static int16_t touch_start_x = 0;
static int32_t scroll_start = 0;
static unsigned long touch_start_time = 0;
static int32_t momentum_velocity = 0;
static int16_t last_tx = 0;

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

  char lineBuf[256];
  album_count = 0;

  while (f.available() && album_count < MAX_ALBUMS) {
    int len = 0;
    while (f.available() && len < 255) {
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
    strncpy(album_filenames[i], fields[0], 63);
    album_filenames[i][63] = '\0';
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

  // Find LRU slot — only consider slots that were successfully allocated
  int lru = -1;
  for (int s = 0; s < CACHE_SLOTS; s++) {
    if (!sd_img_cache[s])
      continue; // Skip failed slots
    if (lru == -1 || cache_access_time[s] < cache_access_time[lru])
      lru = s;
  }
  if (lru == -1)
    return -1; // No usable cache slots

  // Read from SD
  char path[128];
  snprintf(path, sizeof(path), "/sd_card_albums/%s", album_filenames[index]);

  File f = SD.open(path, FILE_READ);
  if (!f)
    return -1;

  size_t bytesRead = f.read((uint8_t *)sd_img_cache[lru], IMG_PIXELS * 2);
  f.close();

  if (bytesRead != (size_t)(IMG_PIXELS * 2))
    return -1;

  // Byte-swap: .bin files are big-endian, ESP32 is little-endian
  for (int i = 0; i < IMG_PIXELS; i++) {
    uint16_t v = sd_img_cache[lru][i];
    sd_img_cache[lru][i] = (v >> 8) | (v << 8);
  }

  cache_album_idx[lru] = index;
  cache_access_time[lru] = millis();
  return lru;
}

static void drawScaledImage(const uint16_t *src, int srcW, int srcH, int dx,
                            int dy, int dstW, int dstH) {
  if (dstW == srcW && dstH == srcH) {
    // Fast path: no scaling, use pushImage with byte swap for SPI
    spr.setSwapBytes(true);
    spr.pushImage(dx, dy, srcW, srcH, src);
    spr.setSwapBytes(false);
  } else {
    // Nearest-neighbor downscale via drawPixel (no swap needed)
    for (int y = 0; y < dstH; y++) {
      int srcY = y * srcH / dstH;
      for (int x = 0; x < dstW; x++) {
        int srcX = x * srcW / dstW;
        spr.drawPixel(dx + x, dy + y, src[srcY * srcW + srcX]);
      }
    }
  }
}

static void drawAlbumArt(int x, int y, int w, int h, int index) {
  if (w < 6 || h < 6)
    return;

  int slot = loadAlbumImage(index);
  if (slot >= 0) {
    drawScaledImage(sd_img_cache[slot], IMG_SRC_SIZE, IMG_SRC_SIZE, x, y, w, h);
  } else {
    spr.fillRoundRect(x, y, w, h, 4, fallback_color);
  }
}

// ============================================================
// Cover Flow Layout
// ============================================================

static int32_t iLerp(int32_t a, int32_t b, int32_t t) {
  if (t <= 0)
    return a;
  if (t >= 100)
    return b;
  return a + ((b - a) * t) / 100;
}

static int getCoverFlowPos(int32_t offset_fp, int *out_w, int *out_h,
                           int *out_cx) {
  int32_t absOff = abs(offset_fp);
  int sign = (offset_fp < 0) ? -1 : ((offset_fp > 0) ? 1 : 0);

  if (absOff <= SCROLL_SCALE) {
    int height = iLerp(CENTER_SIZE, SIDE_SIZE, absOff);
    int width = iLerp(CENTER_SIZE, SIDE_SIZE * 55 / 100, absOff);
    int32_t t_curve = absOff * 85 / 100;
    int cx_offset = sign * iLerp(0, SIDE_OFFSET, t_curve > 100 ? 100 : t_curve);
    *out_w = width;
    *out_h = height;
    *out_cx = SCREEN_W / 2 + cx_offset;
    return (absOff < 30) ? 0 : 1;
  } else if (absOff <= 2 * SCROLL_SCALE) {
    int32_t t = absOff - SCROLL_SCALE;
    int height = iLerp(SIDE_SIZE, FAR_SIZE, t);
    int width = iLerp(SIDE_SIZE * 55 / 100, FAR_SIZE * 40 / 100, t);
    int cx_offset = sign * iLerp(SIDE_OFFSET, FAR_OFFSET, t);
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

  // Collect visible albums
  struct AlbumDraw {
    int index, w, h, cx, depth;
    int32_t absOffset;
  };
  AlbumDraw items[7]; // Max visible: center + 2 sides + 2 far + margin
  int itemCount = 0;

  for (int i = 0; i < album_count; i++) {
    int32_t offset_fp = (int32_t)i * SCROLL_SCALE - scroll_pos;
    int w, h, cx;
    int depth = getCoverFlowPos(offset_fp, &w, &h, &cx);

    if (depth < 3 && w > 5 && itemCount < 7) {
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

  // Draw album art
  for (int n = 0; n < itemCount; n++) {
    int i = items[n].index;
    int w = items[n].w;
    int h = items[n].h;
    int x = items[n].cx - w / 2;
    int y = (SPRITE_H - h) / 2;

    if (x + w > 0 && x < SCREEN_W) {
      drawAlbumArt(x, y, w, h, i);
    }
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
  int16_t tx, ty;
  bool touched = get_touch_coords(&tx, &ty);

  // --- Rotary encoder input ---
  extern int32_t get_encoder_delta();
  int32_t enc = get_encoder_delta();
  if (enc != 0 && !is_dragging) {
    int current_album = constrain(
        (target_scroll + SCROLL_SCALE / 2) / SCROLL_SCALE, 0, album_count - 1);
    int next_album = constrain(current_album + enc, 0, album_count - 1);
    target_scroll = (int32_t)next_album * SCROLL_SCALE;
    Serial.print("ENC enc=");
    Serial.print(enc);
    Serial.print(" cur=");
    Serial.print(current_album);
    Serial.print(" next=");
    Serial.print(next_album);
    Serial.print(" tgt=");
    Serial.print(target_scroll);
    Serial.print(" pos=");
    Serial.println(scroll_pos);
  }

  unsigned long now = millis();
  unsigned long dt = now - last_update_time;
  if (dt > 100)
    dt = 16;
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

      int32_t new_vel =
          ((int32_t)(last_tx - tx) * SCROLL_SCALE) / DRAG_SENSITIVITY;
      momentum_velocity = (momentum_velocity + new_vel) / 2;
      last_tx = tx;

      int32_t lo = -OVERSCROLL_LIMIT;
      int32_t hi = (int32_t)(album_count - 1) * SCROLL_SCALE + OVERSCROLL_LIMIT;
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
                           album_count - 1);
        Serial.print("Tapped: ");
        Serial.println(album_titles[ci]);
        if (album_uris[ci][0] != '\0') {
          spotify_play_album(album_uris[ci]);
        }
        target_scroll = (int32_t)ci * SCROLL_SCALE;
      } else {
        target_scroll = scroll_pos + momentum_velocity * MOMENTUM_MULT / 10;
      }

      target_scroll = constrain(target_scroll, (int32_t)0,
                                (int32_t)(album_count - 1) * SCROLL_SCALE);
      int closest = constrain((target_scroll + SCROLL_SCALE / 2) / SCROLL_SCALE,
                              0, album_count - 1);
      target_scroll = (int32_t)closest * SCROLL_SCALE;
    }
  }

  // Delta-time easing
  if (!is_dragging && scroll_pos != target_scroll) {
    int32_t diff = target_scroll - scroll_pos;
    int32_t step = diff * (int32_t)dt / 100;
    if (step == 0)
      step = (diff > 0) ? 1 : -1;
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