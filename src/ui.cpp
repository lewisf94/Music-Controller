#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"

#define ALBUM_COUNT 6
#define ALBUM_SIZE  120
#define ALBUM_GAP   30
#define SCREEN_W    320
#define SCREEN_H    240
#define ZOOM_MAX    256
#define ZOOM_MIN    180

static lv_color_t album_colors[ALBUM_COUNT] = {
    lv_color_make(231, 76,  60),
    lv_color_make(52,  152, 219),
    lv_color_make(46,  204, 113),
    lv_color_make(243, 156, 18),
    lv_color_make(155, 89,  182),
    lv_color_make(26,  188, 156),
};

static const char* album_names[ALBUM_COUNT] = {
    "Rumours",
    "OK Computer",
    "Dark Side",
    "Abbey Road",
    "Nevermind",
    "Kind of Blue",
};

static lv_obj_t* carousel;
static lv_obj_t* albums[ALBUM_COUNT];
static lv_obj_t* lbl_album_name;
static int current_album = 0;

static void update_zoom()
{
    int32_t scroll_x = lv_obj_get_scroll_x(carousel);

    for (int i = 0; i < ALBUM_COUNT; i++) {
        int32_t album_left = lv_obj_get_x(albums[i]);
        int32_t album_center_on_screen = album_left - scroll_x + ALBUM_SIZE / 2;
        int32_t dist = abs(album_center_on_screen - SCREEN_W / 2);

        int32_t zoom = ZOOM_MAX - ((ZOOM_MAX - ZOOM_MIN) * dist / 160);
        if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
        if (zoom < ZOOM_MIN) zoom = ZOOM_MIN;

        lv_obj_set_style_transform_zoom(albums[i], (uint16_t)zoom, LV_PART_MAIN);
    }
}

static void on_scroll(lv_event_t* e)
{
    static uint32_t last_update = 0;
    uint32_t now = millis();
    if (now - last_update >= 16) {
        last_update = now;
        update_zoom();
    }
}

static void on_scroll_end(lv_event_t* e)
{
    int32_t scroll_x = lv_obj_get_scroll_x(carousel);
    int closest = 0;
    int32_t min_dist = 999999;

    for (int i = 0; i < ALBUM_COUNT; i++) {
        int32_t center = lv_obj_get_x(albums[i]) - scroll_x + ALBUM_SIZE / 2;
        int32_t dist = abs(center - SCREEN_W / 2);
        if (dist < min_dist) {
            min_dist = dist;
            closest = i;
        }
    }

    current_album = closest;
    lv_label_set_text(lbl_album_name, album_names[current_album]);

    Serial.print("Selected: ");
    Serial.println(album_names[current_album]);
}

void ui_init()
{
    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    carousel = lv_obj_create(screen);
    lv_obj_set_size(carousel, SCREEN_W, ALBUM_SIZE + 10);
    lv_obj_align(carousel, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_set_style_bg_color(carousel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(carousel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(carousel, 0, 0);
    lv_obj_set_style_pad_top(carousel, 0, 0);
    lv_obj_set_style_pad_bottom(carousel, 0, 0);

    int side_pad = (SCREEN_W - ALBUM_SIZE) / 2;
    lv_obj_set_style_pad_left(carousel,  side_pad, 0);
    lv_obj_set_style_pad_right(carousel, side_pad, 0);

    lv_obj_set_scroll_dir(carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(carousel, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_layout(carousel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(carousel, ALBUM_GAP, 0);

    for (int i = 0; i < ALBUM_COUNT; i++) {
        albums[i] = lv_obj_create(carousel);
        lv_obj_set_size(albums[i], ALBUM_SIZE, ALBUM_SIZE);

        lv_obj_set_style_bg_color(albums[i], album_colors[i], 0);
        lv_obj_set_style_bg_opa(albums[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(albums[i], 14, 0);
        lv_obj_set_style_border_width(albums[i], 0, 0);
        lv_obj_set_style_pad_all(albums[i], 0, 0);

        lv_obj_clear_flag(albums[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_transform_pivot_x(albums[i], ALBUM_SIZE / 2, 0);
        lv_obj_set_style_transform_pivot_y(albums[i], ALBUM_SIZE / 2, 0);

        lv_obj_t* lbl = lv_label_create(albums[i]);
        lv_label_set_text(lbl, album_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    lbl_album_name = lv_label_create(screen);
    lv_label_set_text(lbl_album_name, album_names[0]);
    lv_obj_set_style_text_color(lbl_album_name, lv_color_white(), 0);
    lv_obj_align(lbl_album_name, LV_ALIGN_TOP_MID, 0, ALBUM_SIZE + 40);

    lv_obj_add_event_cb(carousel, on_scroll,     LV_EVENT_SCROLL,     NULL);
    lv_obj_add_event_cb(carousel, on_scroll_end, LV_EVENT_SCROLL_END, NULL);

    lv_obj_update_layout(carousel);
    update_zoom();

    lv_obj_scroll_to_view(albums[0], LV_ANIM_OFF);
}

void ui_show_album_browser() {}
void ui_show_now_playing()   {}