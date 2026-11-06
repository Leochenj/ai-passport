#include "heart_ui.h"

#include "lvgl.h"
#include <stddef.h>
#include <stdio.h>

LV_FONT_DECLARE(heart_font_80);

#define FRAME_W 240
#define FRAME_H 320
#define FRAME_BYTES (FRAME_W * FRAME_H * 2)
#define FRAME_COUNT 40
#define VIDEO_PERIOD_MS 200

#define BPM_INK 0x0B1220

// Embedded by main/CMakeLists.txt (EMBED_FILES).
extern const uint8_t heart_bg_start[] asm("_binary_heart_bg_bin_start");
extern const uint8_t heart_bg_end[] asm("_binary_heart_bg_bin_end");

static lv_obj_t *s_bg;
static lv_obj_t *s_bpm_label;
static lv_image_dsc_t s_frames[FRAME_COUNT];
static uint16_t s_frame;
static uint16_t s_last_bpm = UINT16_MAX;

static void video_tick(lv_timer_t *timer)
{
    (void)timer;
    s_frame = (uint16_t)((s_frame + 1u) % FRAME_COUNT);
    lv_image_set_src(s_bg, &s_frames[s_frame]);
}

static bool init_frames(void)
{
    const ptrdiff_t embedded = heart_bg_end - heart_bg_start;
    if (embedded != (ptrdiff_t)FRAME_BYTES * FRAME_COUNT) {
        LV_LOG_ERROR("heart_bg.bin has %d bytes, expected %d", (int)embedded,
                     (int)(FRAME_BYTES * FRAME_COUNT));
        return false;
    }

    for (uint16_t i = 0; i < FRAME_COUNT; i++) {
        lv_image_dsc_t *descriptor = &s_frames[i];
        lv_memzero(descriptor, sizeof(*descriptor));
        descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
        descriptor->header.flags = 0;
        descriptor->header.w = FRAME_W;
        descriptor->header.h = FRAME_H;
        descriptor->header.stride = FRAME_W * 2;
        descriptor->data_size = FRAME_BYTES;
        descriptor->data = heart_bg_start + (size_t)i * FRAME_BYTES;
    }
    return true;
}

static void set_bpm_text(uint16_t bpm)
{
    char buffer[8];
    if (bpm == 0) {
        snprintf(buffer, sizeof(buffer), "--");
    } else {
        snprintf(buffer, sizeof(buffer), "%u", (unsigned)bpm);
    }
    lv_label_set_text(s_bpm_label, buffer);
}

void heart_ui_create(void)
{
    if (!init_frames()) return;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);

    s_bg = lv_image_create(screen);
    lv_obj_remove_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(s_bg, &s_frames[0]);
    lv_obj_set_pos(s_bg, 0, 0);

    s_bpm_label = lv_label_create(screen);
    lv_obj_set_width(s_bpm_label, 220);
    lv_obj_set_style_pad_all(s_bpm_label, 6, 0);
    lv_obj_set_style_text_align(s_bpm_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_bpm_label, &heart_font_80, 0);
    lv_obj_set_style_text_color(s_bpm_label, lv_color_hex(BPM_INK), 0);
    lv_obj_set_style_text_outline_stroke_color(s_bpm_label, lv_color_white(), 0);
    lv_obj_set_style_text_outline_stroke_width(s_bpm_label, 5, 0);
    lv_obj_align(s_bpm_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    set_bpm_text(0);

    lv_timer_create(video_tick, VIDEO_PERIOD_MS, NULL);

    lv_screen_load(screen);
}

void heart_ui_set_bpm(uint16_t bpm)
{
    if (s_bpm_label == NULL || bpm == s_last_bpm) return;
    s_last_bpm = bpm;
    set_bpm_text(bpm);
}
