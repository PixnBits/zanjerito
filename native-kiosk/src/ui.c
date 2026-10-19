#include "ui.h"

#include "data.h"
#include "platform.h"
#include "zk_actions.h"
#include "zk_copy.h"
#include "zk_flow.h"
#include "zk_layout.h"
#include "zk_model.h"
#include "zk_repaint.h"
#include "zk_soilcfg.h"
#include "zk_text.h"

#include "generated/zk_fonts.h"
#include "generated/zk_icons.h"

#include "src/core/lv_refr_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Direction C. Flat fills, 2 px borders, no shadow. Label backgrounds stay
 * transparent so the card shows through; that is glyph coverage, not a scrim. */

enum {
    COL_SAND = 0xFBECD6,
    COL_CREAM = 0xFFFAEE,
    COL_EDGE = 0xEEDDC3,
    COL_TILE = 0xFCF5E6,
    COL_LINE = 0xD9C3A0,
    COL_DIM = 0xF3E1C4,
    COL_INK = 0x4A2F12,
    COL_TEXT = 0x3A2A1F,
    COL_MUTED = 0x6E5A45,
    COL_LABEL = 0x7A5A3A,
    COL_TERRA = 0xD26A43,
    COL_TERRA_INK = 0xB04F2C,
    COL_ERR = 0xA8432A,
    COL_TEAL = 0x0F6E6C,
    COL_TEAL_INK = 0x1F6A64,
    COL_PLUM = 0x4E3F63,
    COL_PLUM_INK = 0xF3EEF8,
    COL_PLUM_MUT = 0xD9CFE6,
    COL_STOP = 0xB5361A,
    COL_BAND = 0x7C2411,
    COL_WHITE = 0xFFFFFF,
    COL_TRACK = 0xE3D0B3,
    COL_STALE = 0xBFAE97,
    COL_ON_TRACK = 0x3F8B89,
    COL_ON_FILL = 0xBFEDE6
};

enum { HIT_N = ZK_TARGET_SCHED_NEXT + 1, TG_MAX = 24 };

typedef struct {
    lv_obj_t *hero;
    lv_obj_t *h_label;
    lv_obj_t *h_head;
    lv_obj_t *h_time;
    lv_obj_t *h_sub;
    lv_obj_t *warn;
    lv_obj_t *rain;
    lv_obj_t *rain_tx;
    lv_obj_t *rain_meter;
    lv_obj_t *rain_fill;
    lv_obj_t *rain_0;
    lv_obj_t *rain_1;
    lv_obj_t *tile[ZK_MAX_SHOWN];
    lv_obj_t *title[ZK_MAX_SHOWN];
    lv_obj_t *title_ln[ZK_MAX_SHOWN];
    lv_obj_t *title_rest[ZK_MAX_SHOWN];
    lv_obj_t *drop[ZK_MAX_SHOWN];
    lv_obj_t *slot[ZK_MAX_SHOWN];
    lv_obj_t *bar[ZK_MAX_SHOWN];
    lv_obj_t *fill[ZK_MAX_SHOWN];
    lv_obj_t *slot_tx[ZK_MAX_SHOWN];
    lv_obj_t *sched_btn;
    lv_obj_t *sched_ic;
    lv_obj_t *sched_tx;
    lv_obj_t *pause;
    lv_obj_t *pause_ic;
    lv_obj_t *pause_tx;
    lv_obj_t *stop;
    lv_obj_t *stop_ic;
    lv_obj_t *stop_tx;
    lv_obj_t *sheet;
    lv_obj_t *s_label;
    lv_obj_t *s_title;
    lv_obj_t *s_time;
    lv_obj_t *s_body;
    lv_obj_t *minus;
    lv_obj_t *minus_bar;
    lv_obj_t *minus_tx;
    lv_obj_t *plus;
    lv_obj_t *plus_h;
    lv_obj_t *plus_v;
    lv_obj_t *plus_tx;
    lv_obj_t *value_box;
    lv_obj_t *value;
    lv_obj_t *cancel;
    lv_obj_t *cancel_tx;
    lv_obj_t *run;
    lv_obj_t *run_ic;
    lv_obj_t *run_tx;
    lv_obj_t *stop_all;
    lv_obj_t *stop_all_ic;
    lv_obj_t *stop_all_tx;
    lv_obj_t *ok;
    lv_obj_t *ok_tx;
    lv_obj_t *sched;
    lv_obj_t *sc_label;
    lv_obj_t *sc_name[2];
    lv_obj_t *sc_meta[2];
    lv_obj_t *sc_note;
    lv_obj_t *sc_empty;
    lv_obj_t *sc_empty_body;
    lv_obj_t *next;
    lv_obj_t *next_tx;
    lv_obj_t *hit[HIT_N];
    int n_tiles;
} widgets_t;

typedef struct {
    int valid;
    int screen;
    int unreach;
    int phase;
    int paused;
    int style;
    int overlap;
    int run_tile;
    int minutes;
    int rain_on;
    char rain_text[72];
    char slots[ZK_MAX_SHOWN][96];
    char time_line[96];
    int n_stations;
} frame_t;

enum { LAB_TRACK_MAX = 96, LAB_ID_STORE = 32 };

typedef struct {
    char id[LAB_ID_STORE];
    lv_obj_t *obj;
    int clamped;
} lab_slot_t;

static widgets_t g_w;
static lab_slot_t g_labs[LAB_TRACK_MAX];
static int g_nlabs;
static int g_lab_overflow;
static const char *g_mark_id;
static int g_mark_clamped;
static char g_idbuf[LAB_ID_STORE];

static zk_app_t g_app;
static char g_fix[512];
static lv_display_t *g_disp;
static int g_built;
static int g_ready;
static int g_override = -1;
static int g_pressed;
static enum zk_screen g_screen;
static enum zk_screen g_under;
static int g_sched_i;
static int g_station;
static int g_minutes = 10;
static int g_min_key = -999;
static int g_paused;
static zk_snapshot_t g_snap;
static zk_view_t g_view;
static zk_layout_in g_lin;
static zk_target g_tg[TG_MAX];
static int g_ntg;
static zk_stop_flow_t g_stop;
static int g_stop_hold = -1;
static int g_last_retry_set;
static int64_t g_last_retry;
static int64_t g_last_tap;
static uint32_t g_seen_seq;
static int g_seen_ok;
static uint32_t g_ack_gen;
static int g_confirmed_reachable;
static int g_have_prev_flags;
static int g_prev_paused;
static int g_prev_unreach;
static zk_wall_t g_confirmed_at;
static int g_has_confirmed_at;
static int g_retry_pulse;
static int g_rain_yield;
static int64_t g_idle_from;
static int g_idle_ready;
static int g_minutes_hold = -1;
static int g_sched_hold = -1;
static frame_t g_prev;
static zk_soil_cfg_t g_soil;

static uint8_t g_oct44[44 * 44];
static uint8_t g_oct40[40 * 40];
static uint8_t g_play30[30 * 30];
static uint8_t g_play28[28 * 28];
static uint8_t g_drop22[22 * 22];
static uint8_t g_warn22[22 * 22];
static uint8_t g_cal28[28 * 28];
static uint8_t g_back28[28 * 28];
static lv_image_dsc_t g_oct44_dsc;
static lv_image_dsc_t g_oct40_dsc;
static lv_image_dsc_t g_play30_dsc;
static lv_image_dsc_t g_play28_dsc;
static lv_image_dsc_t g_drop22_dsc;
static lv_image_dsc_t g_warn_dsc;
static lv_image_dsc_t g_cal28_dsc;
static lv_image_dsc_t g_back28_dsc;

static lv_color_t hex(uint32_t c)
{
    return lv_color_hex(c);
}

static uint32_t darker(uint32_t c)
{
    int r = (int)((c >> 16) & 255u) * 3 / 4;
    int g = (int)((c >> 8) & 255u) * 3 / 4;
    int b = (int)(c & 255u) * 3 / 4;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void copy_cap(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void tracks_reset(void)
{
    g_nlabs = 0;
    g_lab_overflow = 0;
    g_mark_id = NULL;
    g_mark_clamped = 0;
}

static void mark_lab(const char *id, int clamped)
{
    g_mark_id = id;
    g_mark_clamped = clamped;
}

static void mark_lab_n(const char *prefix, int n, int clamped)
{
    snprintf(g_idbuf, sizeof g_idbuf, "%s%d", prefix, n);
    mark_lab(g_idbuf, clamped);
}

static void remember(lv_obj_t *lb)
{
    lab_slot_t *s;
    if (g_nlabs >= LAB_TRACK_MAX) {
        g_lab_overflow = 1;
        g_mark_id = NULL;
        g_mark_clamped = 0;
        return;
    }
    s = &g_labs[g_nlabs++];
    if (g_mark_id) {
        copy_cap(s->id, sizeof s->id, g_mark_id);
    } else {
        snprintf(s->id, sizeof s->id, "l%d", g_nlabs - 1);
    }
    s->obj = lb;
    s->clamped = g_mark_clamped;
    g_mark_id = NULL;
    g_mark_clamped = 0;
}

static void set_tracked(lv_obj_t *lb, const char *text, int clamped)
{
    int i;
    const char *cur;
    if (!lb) {
        return;
    }
    if (!text) {
        text = "";
    }
    cur = lv_label_get_text(lb);
    if (!cur || strcmp(cur, text) != 0) {
        lv_label_set_text(lb, text);
    }
    for (i = 0; i < g_nlabs; i++) {
        if (g_labs[i].obj == lb) {
            g_labs[i].clamped = clamped;
            return;
        }
    }
}

static void show(lv_obj_t *o, int on)
{
    if (!o) {
        return;
    }
    if (on) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clickable(lv_obj_t *o, int on)
{
    if (!o) {
        return;
    }
    if (on) {
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void flat(lv_obj_t *o)
{
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

static void paint_box(lv_obj_t *o, uint32_t bg, uint32_t border, int width, int radius, lv_border_side_t side)
{
    if (!o) {
        return;
    }
    lv_obj_set_style_bg_color(o, hex(bg), 0);
    lv_obj_set_style_bg_color(o, hex(bg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_side(o, side, 0);
    lv_obj_set_style_border_side(o, side, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(o, width, 0);
    lv_obj_set_style_border_width(o, width, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(o, hex(border), 0);
    lv_obj_set_style_border_color(o, hex(darker(border)), LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(o, width > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void place(lv_obj_t *o, int x, int y, int w, int h)
{
    if (!o) {
        return;
    }
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
}

static lv_obj_t *make_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    flat(o);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_obj_set_style_bg_opa(lb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(lb, font, 0);
    lv_obj_set_style_text_color(lb, hex(color), 0);
    lv_obj_set_style_text_opa(lb, LV_OPA_COVER, 0);
    lv_obj_set_style_text_line_space(lb, 0, 0);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(lb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    remember(lb);
    return lb;
}

/* Visual-only label. Not tracked, so tests keep reading the logical title. */
static lv_obj_t *make_quiet_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_obj_set_style_bg_opa(lb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(lb, font, 0);
    lv_obj_set_style_text_color(lb, hex(color), 0);
    lv_obj_set_style_text_opa(lb, LV_OPA_COVER, 0);
    lv_obj_set_style_text_line_space(lb, 0, 0);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_CLIP);
    lv_obj_remove_flag(lb, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return lb;
}

static void recolor(lv_obj_t *im, uint32_t color)
{
    if (!im) {
        return;
    }
    lv_obj_set_style_image_recolor(im, hex(color), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(im, LV_OPA_TRANSP, 0);
}

static void blit_a8(const lv_image_dsc_t *src, int dw, int dh, uint8_t *dst, lv_image_dsc_t *out)
{
    int sw;
    int sh;
    int y;
    int x;
    memset(out, 0, sizeof *out);
    if (!src || !src->data || dw < 1 || dh < 1) {
        return;
    }
    sw = (int)src->header.w;
    sh = (int)src->header.h;
    if (sw < 1 || sh < 1) {
        return;
    }
    for (y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        const uint8_t *row = src->data + (size_t)sy * (size_t)src->header.stride;
        for (x = 0; x < dw; x++) {
            dst[(size_t)y * (size_t)dw + (size_t)x] = row[x * sw / dw];
        }
    }
    out->header.magic = LV_IMAGE_HEADER_MAGIC;
    out->header.cf = LV_COLOR_FORMAT_A8;
    out->header.w = (uint16_t)dw;
    out->header.h = (uint16_t)dh;
    out->header.stride = (uint16_t)dw;
    out->data_size = (uint32_t)(dw * dh);
    out->data = dst;
}

/* Spec overlap mark: 22 px outline triangle + bang (viewBox 24, stroke 2). */
static const uint8_t k_warn22[22 * 22] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x19, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0xdf, 0xe2, 0x0b, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x82, 0xff, 0xff, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1c, 0xf6, 0xf7, 0xf7, 0xf6, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa5, 0xff,
    0x81, 0x83, 0xff, 0xa4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0xfe, 0xe3, 0x09, 0x0a,
    0xe4, 0xfe, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xc8, 0xff, 0x5b, 0x00, 0x00, 0x5d, 0xff,
    0xc4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x5a, 0xff, 0xc7, 0x01, 0xa3, 0xa1, 0x01, 0xc9, 0xff, 0x59,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
    0xe2, 0xfe, 0x36, 0x02, 0xff, 0xff, 0x02, 0x37, 0xfe, 0xe1, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xff, 0xa2,
    0x00, 0x03, 0xff, 0xff, 0x02, 0x00, 0xa4, 0xff, 0x7b, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1a, 0xf5, 0xf5, 0x1b, 0x00, 0x03,
    0xff, 0xff, 0x02, 0x00, 0x1c, 0xf6, 0xf5, 0x19, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xa0, 0xff, 0x7c, 0x00, 0x00, 0x03, 0xff, 0xff,
    0x02, 0x00, 0x00, 0x7e, 0xff, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x33, 0xfe, 0xe1, 0x09, 0x00, 0x00, 0x02, 0xfe, 0xfd, 0x02, 0x00,
    0x00, 0x09, 0xe3, 0xfd, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc1,
    0xff, 0x58, 0x00, 0x00, 0x00, 0x00, 0x84, 0x83, 0x00, 0x00, 0x00, 0x00,
    0x59, 0xff, 0xc1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xff, 0xc4, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xa3, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc4,
    0xff, 0x52, 0x00, 0x00, 0x00, 0x06, 0xdf, 0xfe, 0x33, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xf1, 0xf0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x36, 0xfe, 0xde,
    0x06, 0x00, 0x00, 0x78, 0xff, 0xb5, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x51, 0x51, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xb7, 0xff, 0x76, 0x00,
    0x05, 0xf1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf1, 0x05, 0x05, 0x9e,
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static void build_warn(void)
{
    memcpy(g_warn22, k_warn22, sizeof g_warn22);
    memset(&g_warn_dsc, 0, sizeof g_warn_dsc);
    g_warn_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_warn_dsc.header.cf = LV_COLOR_FORMAT_A8;
    g_warn_dsc.header.w = 22;
    g_warn_dsc.header.h = 22;
    g_warn_dsc.header.stride = 22;
    g_warn_dsc.data_size = 22 * 22;
    g_warn_dsc.data = g_warn22;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const void *src)
{
    lv_obj_t *im = lv_image_create(parent);
    lv_image_set_src(im, src);
    recolor(im, COL_INK);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return im;
}

static void on_click(lv_event_t *e);

static void listen(lv_obj_t *o)
{
    lv_obj_add_event_cb(o, on_click, LV_EVENT_CLICKED, NULL);
}

typedef struct {
    const lv_font_t *font;
    int32_t ls;
} fit_u_t;

static int fit_width(const char *text, size_t nbytes, void *user)
{
    fit_u_t *u = user;
    if (!text || !u || !u->font) {
        return 0;
    }
    return (int)lv_text_get_width(text, (uint32_t)nbytes, u->font, u->ls);
}

/* 1 when the stored string was end-cut. Headline and sheet titles skip this. */
static int fit_text(char *dst, size_t cap, const char *src, const lv_font_t *font, int ls, int width, int lines)
{
    fit_u_t u;
    if (!dst || cap == 0) {
        return 0;
    }
    dst[0] = 0;
    if (!src) {
        src = "";
    }
    if (!font || width < 1 || lines < 1) {
        zk_str_copy(dst, cap, src);
        return 0;
    }
    u.font = font;
    u.ls = ls;
    if (zk_text_fit(dst, cap, src, width * lines, fit_width, &u) != 0) {
        zk_str_copy(dst, cap, src);
        return 0;
    }
    return strstr(dst, ZK_TEXT_ELLIPSIS) != NULL;
}

static void text_color(lv_obj_t *lb, uint32_t color)
{
    if (lb) {
        lv_obj_set_style_text_color(lb, hex(color), 0);
    }
}

static void use_font(lv_obj_t *lb, const lv_font_t *font, int ls)
{
    if (!lb) {
        return;
    }
    lv_obj_set_style_text_font(lb, font, 0);
    lv_obj_set_style_text_letter_space(lb, ls, 0);
}

static int text_width(lv_obj_t *lb)
{
    const char *t;
    const lv_font_t *f;
    int32_t ls;
    if (!lb) {
        return 0;
    }
    t = lv_label_get_text(lb);
    f = lv_obj_get_style_text_font(lb, LV_PART_MAIN);
    ls = lv_obj_get_style_text_letter_space(lb, LV_PART_MAIN);
    if (!t || !f) {
        return 0;
    }
    return (int)lv_text_get_width(t, (uint32_t)strlen(t), f, ls);
}

static uint32_t utf8_next(const char *s, uint32_t i)
{
    unsigned char c;
    if (!s || !s[i]) {
        return i;
    }
    c = (unsigned char)s[i];
    if (c < 0x80) {
        return i + 1;
    }
    if ((c & 0xE0) == 0xC0 && s[i + 1]) {
        return i + 2;
    }
    if ((c & 0xF0) == 0xE0 && s[i + 1] && s[i + 2]) {
        return i + 3;
    }
    if ((c & 0xF8) == 0xF0 && s[i + 1] && s[i + 2] && s[i + 3]) {
        return i + 4;
    }
    return i + 1;
}

/* Bytes of the next line inside s[0, nbytes) that fit in max_w.
 * Breaks on the last space that fits. A word wider than the line breaks
 * on a character boundary. Returns at least one character when nbytes > 0
 * so the caller always advances. */
static uint32_t wrap_line(const char *s, uint32_t nbytes, const lv_font_t *font, int max_w)
{
    uint32_t i = 0;
    uint32_t last_space = 0;
    uint32_t last_fit = 0;
    int have_space = 0;
    if (!s || nbytes == 0 || !font || max_w < 1) {
        return 0;
    }
    while (i < nbytes && s[i]) {
        uint32_t n;
        int w;
        if (s[i] == '\n') {
            return i;
        }
        n = utf8_next(s, i);
        if (n == i || n > nbytes) {
            break;
        }
        w = (int)lv_text_get_width(s, n, font, 0);
        if (w > max_w) {
            if (have_space && last_space > 0) {
                return last_space;
            }
            if (last_fit > 0) {
                return last_fit;
            }
            return n;
        }
        last_fit = n;
        if (s[n - 1] == ' ') {
            last_space = n;
            have_space = 1;
        }
        i = n;
    }
    return i > 0 ? i : 0;
}

/* Visual lines for a tile name.
 * Spaced wraps stay in the original string so LVGL can break them.
 * A word wider than the line gets a newline (character break).
 * Ellipsis is added only on the last line, and only when text remains.
 * Returns 1 when that ellipsis was used. *nlines_out is at least 1 for
 * a non-empty source. */
static int fit_wrapped(char *dst, size_t cap, const char *src, const lv_font_t *font, int first_w, int rest_w,
                       int max_lines, int *nlines_out)
{
    enum { SEG_MAX = 8 };
    struct {
        uint32_t off;
        uint32_t n;
        int hard;
    } seg[SEG_MAX];
    const char *p;
    int nlines = 0;
    int overflow = 0;
    int any_hard = 0;
    int i;
    if (!dst || cap == 0) {
        return 0;
    }
    dst[0] = 0;
    if (!src) {
        src = "";
    }
    if (!font || max_lines < 1 || first_w < 1) {
        copy_cap(dst, cap, src);
        if (nlines_out) {
            *nlines_out = src[0] ? 1 : 0;
        }
        return 0;
    }
    if (max_lines > SEG_MAX) {
        max_lines = SEG_MAX;
    }
    p = src;
    while (*p && nlines < max_lines) {
        int w = nlines == 0 ? first_w : rest_w;
        uint32_t adv;
        while (*p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (w < 1) {
            w = first_w;
        }
        adv = wrap_line(p, (uint32_t)strlen(p), font, w);
        if (adv == 0) {
            break;
        }
        seg[nlines].off = (uint32_t)(p - src);
        seg[nlines].n = adv;
        seg[nlines].hard = (p[adv] != '\0' && p[adv - 1] != ' ') ? 1 : 0;
        any_hard |= seg[nlines].hard;
        p += adv;
        if (*p == '\n') {
            p++;
        }
        nlines++;
    }
    overflow = *p != '\0';
    if (nlines < 1) {
        copy_cap(dst, cap, src);
        if (nlines_out) {
            *nlines_out = src[0] ? 1 : 0;
        }
        return 0;
    }
    /* A narrower first line (the running tile's inline drop) needs explicit
     * breaks so the hanging indent wraps the same way it was measured. */
    if (!overflow && !any_hard && (first_w == rest_w || nlines <= 1)) {
        copy_cap(dst, cap, src);
        if (nlines_out) {
            *nlines_out = nlines;
        }
        return 0;
    }
    {
        size_t used = 0;
        int last = overflow ? nlines - 1 : nlines;
        int clamped = overflow ? 1 : 0;
        for (i = 0; i < last; i++) {
            uint32_t n = seg[i].n;
            const char *s = src + seg[i].off;
            while (n > 0 && s[n - 1] == ' ') {
                n--;
            }
            if (used && used + 1 < cap) {
                dst[used++] = '\n';
            }
            if (n >= cap - used) {
                n = (uint32_t)(cap - used - 1);
            }
            memcpy(dst + used, s, n);
            used += n;
            dst[used] = 0;
        }
        if (overflow) {
            char ell[ZK_TITLE_MAX];
            uint32_t n;
            int w = nlines <= 1 ? first_w : rest_w;
            fit_text(ell, sizeof ell, src + seg[nlines - 1].off, font, 0, w, 1);
            if (used && used + 1 < cap) {
                dst[used++] = '\n';
            }
            n = (uint32_t)strlen(ell);
            if (n >= cap - used) {
                n = (uint32_t)(cap - used - 1);
            }
            memcpy(dst + used, ell, n);
            used += n;
            dst[used] = 0;
        }
        if (nlines_out) {
            *nlines_out = nlines;
        }
        return clamped;
    }
}

static void copy_span(char *dst, size_t cap, const char *s, uint32_t n)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!s) {
        dst[0] = 0;
        return;
    }
    while (n > 0 && s[n - 1] == ' ') {
        n--;
    }
    if (n >= cap) {
        n = (uint32_t)cap - 1;
    }
    memcpy(dst, s, n);
    dst[n] = 0;
}

static void center_pair(lv_obj_t *btn, lv_obj_t *icon, int iw, int ih, lv_obj_t *word, int gap)
{
    int bw;
    int bh;
    int tw;
    int th;
    int total;
    int x;
    int y;
    int wy;
    if (!btn || !word) {
        return;
    }
    /* place() writes the style size. Coords stay stale until a layout pass,
     * and a previous sheet (352 px Cancel) would center into the wrong box. */
    lv_obj_update_layout(btn);
    bw = (int)lv_obj_get_width(btn);
    bh = (int)lv_obj_get_height(btn);
    tw = text_width(word);
    th = (int)lv_font_get_line_height(lv_obj_get_style_text_font(word, LV_PART_MAIN));
    if (tw < 1) {
        tw = 1;
    }
    if (th < 1) {
        th = 1;
    }
    lv_label_set_long_mode(word, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(word, tw, th);
    wy = (bh - th) / 2;
    if (wy < 0) {
        wy = 0;
    }
    if (icon && iw > 0) {
        total = iw + gap + tw;
        x = (bw - total) / 2;
        if (x < 4) {
            x = 4;
        }
        y = (bh - ih) / 2;
        if (y < 0) {
            y = 0;
        }
        lv_obj_set_pos(icon, x, y);
        lv_obj_set_pos(word, x + iw + gap, wy);
        show(icon, 1);
    } else {
        x = (bw - tw) / 2;
        if (x < 0) {
            x = 0;
        }
        lv_obj_set_pos(word, x, wy);
        if (icon) {
            show(icon, 0);
        }
    }
}

/* Icon over the word. The 120 px Schedules button cannot fit them side by side. */
static void stack_pair(lv_obj_t *btn, lv_obj_t *icon, int iw, int ih, lv_obj_t *word, int gap)
{
    int bw;
    int bh;
    int tw;
    int th;
    int total;
    int x;
    int y;
    if (!btn || !word) {
        return;
    }
    lv_obj_update_layout(btn);
    bw = (int)lv_obj_get_width(btn);
    bh = (int)lv_obj_get_height(btn);
    tw = text_width(word);
    th = (int)lv_font_get_line_height(lv_obj_get_style_text_font(word, LV_PART_MAIN));
    if (tw < 1) {
        tw = 1;
    }
    if (th < 1) {
        th = 1;
    }
    if (tw > bw - 8) {
        tw = bw - 8;
    }
    if (tw < 1) {
        tw = 1;
    }
    lv_label_set_long_mode(word, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(word, tw, th);
    total = (icon && iw > 0 ? ih + gap : 0) + th;
    y = (bh - total) / 2;
    if (y < 2) {
        y = 2;
    }
    x = (bw - tw) / 2;
    if (x < 2) {
        x = 2;
    }
    if (icon && iw > 0) {
        int ix = (bw - iw) / 2;
        if (ix < 0) {
            ix = 0;
        }
        lv_obj_set_pos(icon, ix, y);
        lv_obj_set_pos(word, x, y + ih + gap);
        show(icon, 1);
    } else {
        lv_obj_set_pos(word, x, y);
        if (icon) {
            show(icon, 0);
        }
    }
}

static int listed(enum zk_target_id id)
{
    int i;
    for (i = 0; i < g_ntg; i++) {
        if (g_tg[i].id == id) {
            return 1;
        }
    }
    return 0;
}

static enum zk_target_id wid_of(const lv_obj_t *o)
{
    int i;
    if (!o) {
        return ZK_TARGET_NONE;
    }
    if (o == g_w.hero) {
        return ZK_TARGET_HERO;
    }
    if (o == g_w.pause) {
        return g_paused ? ZK_TARGET_RESUME : ZK_TARGET_PAUSE;
    }
    if (o == g_w.sched_btn) {
        return g_screen == ZK_SCREEN_SCHEDULES ? ZK_TARGET_HOME : ZK_TARGET_SCHEDULES;
    }
    if (o == g_w.next) {
        return ZK_TARGET_SCHED_NEXT;
    }
    if (o == g_w.stop) {
        return ZK_TARGET_STOP;
    }
    if (o == g_w.minus) {
        return ZK_TARGET_MINUS;
    }
    if (o == g_w.plus) {
        return ZK_TARGET_PLUS;
    }
    if (o == g_w.cancel) {
        return ZK_TARGET_CANCEL;
    }
    if (o == g_w.run) {
        return ZK_TARGET_RUN;
    }
    if (o == g_w.stop_all) {
        return ZK_TARGET_STOP_ALL;
    }
    if (o == g_w.ok) {
        return ZK_TARGET_OK;
    }
    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        if (o == g_w.tile[i]) {
            return (enum zk_target_id)(ZK_TARGET_TILE0 + i);
        }
    }
    return ZK_TARGET_NONE;
}

static int sheet_kind_of(enum zk_screen screen)
{
    switch (screen) {
    case ZK_SCREEN_SHEET_RUN:
        return ZK_SHEET_RUN;
    case ZK_SCREEN_SHEET_ANYWAY:
        return ZK_SHEET_ANYWAY;
    case ZK_SCREEN_SHEET_STOP_STATION:
        return ZK_SHEET_STOP_STATION;
    case ZK_SCREEN_SHEET_STOP_ALL:
        return ZK_SHEET_STOP_ALL;
    case ZK_SCREEN_SHEET_OVERLAP:
        return ZK_SHEET_OVERLAP;
    default:
        return ZK_SHEET_NONE;
    }
}

/* Hero, rain, and tiles hide under a confirm sheet and under the schedules list. */
static int top_covered(void)
{
    return sheet_kind_of(g_screen) != ZK_SHEET_NONE || g_screen == ZK_SCREEN_SCHEDULES;
}

static int unreachable_of(const zk_snapshot_t *s)
{
    return !s->have_kiosk || s->api_state != ZK_API_OK || s->stale;
}

static int watering_of(const zk_snapshot_t *s)
{
    const zk_kiosk_t *k;
    if (!s->have_kiosk) {
        return 0;
    }
    k = &s->kiosk;
    return (k->watering || k->has_run || k->n_on > 0 || k->current_station[0]) ? 1 : 0;
}

static int manual_of(const zk_snapshot_t *s)
{
    if (!s->have_kiosk || !s->kiosk.has_run) {
        return 0;
    }
    if (strcmp(s->kiosk.run.kind, "manual") == 0) {
        return 1;
    }
    return strcmp(s->kiosk.run.program, "manual") == 0;
}

static void note_actions(const zk_snapshot_t *s)
{
    zk_action_result_t r;
    zk_data_action_result(&r);
    if (!r.done) {
        return;
    }
    if (g_seen_ok && r.seq == g_seen_seq) {
        return;
    }
    g_seen_ok = 1;
    g_seen_seq = r.seq;
    if (r.kind != ZK_ACT_STOP || g_stop_hold >= 0) {
        return;
    }
    zk_stop_http(&g_stop, r.code == ZK_OK);
    if (g_stop.phase == ZK_STOP_WAIT && s) {
        g_ack_gen = s->kiosk_gen;
    }
}

static void submit_stop_now(void)
{
    zk_data_submit_action(ZK_ACT_STOP, NULL);
    g_last_retry = zk_platform_mono_ms();
    g_last_retry_set = 1;
}

static void advance_stop(const zk_snapshot_t *s, int unreachable, int watering)
{
    int send;
    zk_stop_snap_t snap;
    if (g_stop_hold >= 0 || !s) {
        return;
    }
    if (g_stop.phase != ZK_STOP_WAIT && g_stop.phase != ZK_STOP_QUEUED) {
        return;
    }
    if (g_stop.inflight) {
        return;
    }
    if (unreachable) {
        /* Unreachable is sticky across frames. WAIT means the last POST got
         * 2xx and there is still no all-off snapshot. Hand the resend to the
         * 2s retry timer. Calling the snapshot machine here would POST again
         * on the next tick and spin. */
        if (g_stop.phase == ZK_STOP_WAIT) {
            g_stop.phase = ZK_STOP_QUEUED;
            g_stop.failed = 1;
            g_stop.inflight = 0;
            g_stop.again = 0;
            return;
        }
        snap = ZK_STOP_SNAP_UNAVAILABLE;
    } else if (s->kiosk_gen == g_ack_gen) {
        /* The poll visible at ack time is not a decision. A later parse is. */
        return;
    } else {
        snap = watering ? ZK_STOP_SNAP_STILL_ON : ZK_STOP_SNAP_CLEAR;
    }
    send = zk_stop_snapshot(&g_stop, snap);
    if (g_stop.phase == ZK_STOP_CONFIRMED) {
        g_confirmed_reachable = unreachable ? 0 : 1;
        g_confirmed_at = s->wall;
        g_has_confirmed_at = 1;
    }
    if (send) {
        submit_stop_now();
    }
}

static void maybe_clear_confirmed(int unreachable, int watering, int manual, int paused)
{
    int lost;
    if (g_stop_hold >= 0 || g_stop.phase != ZK_STOP_CONFIRMED || !g_have_prev_flags) {
        return;
    }
    lost = g_confirmed_reachable && !g_prev_unreach && unreachable;
    if (watering || manual || paused != g_prev_paused || lost) {
        g_stop.phase = ZK_STOP_IDLE;
        g_stop.inflight = 0;
        g_stop.again = 0;
        g_stop.failed = 0;
        g_has_confirmed_at = 0;
    }
}

static void maybe_retry(void)
{
    int64_t now;
    if (g_stop_hold >= 0 || g_stop.phase != ZK_STOP_QUEUED || g_stop.inflight) {
        return;
    }
    now = zk_platform_mono_ms();
    if (g_last_retry_set && now >= g_last_retry && now - g_last_retry < 2000) {
        return;
    }
    if (!zk_stop_retry(&g_stop)) {
        return;
    }
    submit_stop_now();
    g_retry_pulse = 1;
    note_actions(&g_snap);
}

void zk_ui_note_touch(void);

static void apply_id(enum zk_target_id id)
{
    zk_flow_tile_t tile;
    zk_flow_out_t out;
    int64_t now;
    int ti;
    if (id == ZK_TARGET_NONE || !listed(id)) {
        return;
    }
    now = zk_platform_mono_ms();
    if (!zk_flow_debounce_ok(g_last_tap, now)) {
        return;
    }
    memset(&tile, 0, sizeof tile);
    tile.paused = g_paused;
    if (id >= ZK_TARGET_TILE0 && id <= ZK_TARGET_TILE5) {
        ti = (int)id - (int)ZK_TARGET_TILE0;
        if (ti >= 0 && ti < g_view.n_tiles && g_view.tiles[ti].vis == ZK_TILE_ON) {
            tile.tile_on = 1;
        }
    }
    zk_flow_tap(id, &tile, &out);
    if (out.effect == ZK_TAP_NONE) {
        return;
    }
    g_last_tap = now;
    zk_ui_note_touch();
    switch (out.effect) {
    case ZK_TAP_OPEN_RUN:
    case ZK_TAP_OPEN_ANYWAY:
        if (id >= ZK_TARGET_TILE0 && id <= ZK_TARGET_TILE5) {
            g_station = (int)id - (int)ZK_TARGET_TILE0;
        }
        g_screen = out.effect == ZK_TAP_OPEN_ANYWAY ? ZK_SCREEN_SHEET_ANYWAY : ZK_SCREEN_SHEET_RUN;
        g_min_key = -999;
        break;
    case ZK_TAP_OPEN_STOP_STATION:
        if (id >= ZK_TARGET_TILE0 && id <= ZK_TARGET_TILE5) {
            g_station = (int)id - (int)ZK_TARGET_TILE0;
        }
        g_screen = ZK_SCREEN_SHEET_STOP_STATION;
        break;
    case ZK_TAP_OPEN_STOP_ALL:
        g_under = g_screen == ZK_SCREEN_SCHEDULES ? ZK_SCREEN_SCHEDULES : ZK_SCREEN_HOME;
        g_screen = ZK_SCREEN_SHEET_STOP_ALL;
        break;
    case ZK_TAP_OPEN_OVERLAP:
        g_screen = ZK_SCREEN_SHEET_OVERLAP;
        break;
    case ZK_TAP_OPEN_SCHEDULES:
        g_sched_i = 0;
        g_screen = ZK_SCREEN_SCHEDULES;
        break;
    case ZK_TAP_HOME:
        g_screen = ZK_SCREEN_HOME;
        g_under = ZK_SCREEN_HOME;
        break;
    case ZK_TAP_SCHED_NEXT: {
        int nsch = g_snap.have_schedules ? g_snap.schedules.n : 0;
        g_sched_i += 2;
        if (nsch <= 0 || g_sched_i >= nsch) {
            g_sched_i = 0;
        }
        break;
    }
    case ZK_TAP_CLOSE:
        g_screen = g_under;
        g_under = ZK_SCREEN_HOME;
        break;
    case ZK_TAP_STEPPER:
        g_minutes += out.minutes_delta;
        if (g_minutes < 1) {
            g_minutes = 1;
        }
        if (g_minutes > 60) {
            g_minutes = 60;
        }
        break;
    case ZK_TAP_SUBMIT_RUN: {
        char body[192];
        const char *sid = "";
        int preempt = 0;
        if (g_snap.have_kiosk && g_station >= 0 && g_station < g_snap.kiosk.n_stations) {
            sid = g_snap.kiosk.stations[g_station].id;
            preempt = watering_of(&g_snap);
        }
        if (zk_action_run_body(body, sizeof body, sid, g_minutes * 60, preempt) == ZK_OK) {
            zk_data_submit_action(ZK_ACT_RUN, body);
        }
        g_screen = ZK_SCREEN_HOME;
        g_under = ZK_SCREEN_HOME;
        note_actions(&g_snap);
        break;
    }
    case ZK_TAP_SUBMIT_STOP:
        if (g_stop_hold < 0 && zk_stop_confirm(&g_stop)) {
            submit_stop_now();
            note_actions(&g_snap);
            advance_stop(&g_snap, unreachable_of(&g_snap), watering_of(&g_snap));
        }
        g_screen = g_under;
        g_under = ZK_SCREEN_HOME;
        break;
    case ZK_TAP_SUBMIT_PAUSE:
        zk_data_submit_action(ZK_ACT_PAUSE, ZK_PAUSE_BODY);
        break;
    case ZK_TAP_SUBMIT_RESUME:
        zk_data_submit_action(ZK_ACT_RESUME, NULL);
        break;
    default:
        break;
    }
}

static void on_click(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    apply_id(wid_of(lv_event_get_target(e)));
}

static int run_index(const zk_view_t *v)
{
    int i;
    for (i = 0; i < v->n_tiles; i++) {
        if (v->tiles[i].vis == ZK_TILE_ON) {
            return i;
        }
    }
    return -1;
}

static int pick_event(const frame_t *prev, const frame_t *now, zk_repaint_ctx_t *ctx)
{
    int i;
    memset(ctx, 0, sizeof *ctx);
    ctx->tile_a = -1;
    ctx->tile_b = -1;
    ctx->n_stations = now->n_stations;
    if (!prev->valid) {
        return ZK_EVT_BOOT;
    }
    if (prev->screen != (int)ZK_SCREEN_SHEET_STOP_ALL && now->screen == (int)ZK_SCREEN_SHEET_STOP_ALL) {
        return ZK_EVT_OPEN_STOP_ALL;
    }
    if (prev->screen == (int)ZK_SCREEN_SHEET_STOP_ALL && now->screen != (int)ZK_SCREEN_SHEET_STOP_ALL) {
        if (now->phase == (int)ZK_STOP_QUEUED && now->unreach) {
            return ZK_EVT_OFFLINE_CONFIRM;
        }
        return ZK_EVT_CLOSE_FULL_SHEET;
    }
    if (prev->screen != now->screen &&
        (prev->screen == (int)ZK_SCREEN_SCHEDULES || now->screen == (int)ZK_SCREEN_SCHEDULES)) {
        return ZK_EVT_SCHEDULES;
    }
    if (prev->screen != now->screen) {
        return ZK_EVT_SHEET;
    }
    if (!prev->unreach && now->unreach) {
        return ZK_EVT_CONTROLLER_LOST;
    }
    if (prev->unreach && !now->unreach) {
        return ZK_EVT_RETURN_ONLINE;
    }
    if (prev->phase != (int)ZK_STOP_CONFIRMED && now->phase == (int)ZK_STOP_CONFIRMED) {
        return ZK_EVT_ACK;
    }
    if (prev->paused != now->paused) {
        return ZK_EVT_PAUSE_RESUME;
    }
    if (prev->style != now->style) {
        if (prev->style == (int)ZK_HERO_MANUAL || now->style == (int)ZK_HERO_MANUAL) {
            ctx->tile_a = now->run_tile >= 0 ? now->run_tile : prev->run_tile;
            return ZK_EVT_MANUAL;
        }
        if (prev->style == (int)ZK_HERO_OVERLAP || now->style == (int)ZK_HERO_OVERLAP) {
            return ZK_EVT_OVERLAP;
        }
        ctx->tile_a = now->run_tile >= 0 ? now->run_tile : prev->run_tile;
        return ZK_EVT_IDLE_WATER;
    }
    if (prev->overlap != now->overlap) {
        return ZK_EVT_OVERLAP;
    }
    if (prev->run_tile != now->run_tile) {
        ctx->tile_a = prev->run_tile;
        ctx->tile_b = now->run_tile;
        return ZK_EVT_NEXT_STATION;
    }
    if (prev->minutes != now->minutes &&
        (now->screen == (int)ZK_SCREEN_SHEET_RUN || now->screen == (int)ZK_SCREEN_SHEET_ANYWAY)) {
        ctx->stepper_cross = (prev->minutes <= 1) != (now->minutes <= 1);
        return ZK_EVT_STEPPER;
    }
    ctx->rain = (prev->rain_on != now->rain_on || strcmp(prev->rain_text, now->rain_text) != 0) ? 1 : 0;
    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        if (strcmp(prev->slots[i], now->slots[i]) != 0) {
            ctx->soil_mask |= (1u << i);
        }
    }
    if (ctx->soil_mask || ctx->rain) {
        return ZK_EVT_SOIL_RAIN;
    }
    if (now->n_stations >= 0 && g_retry_pulse) {
        return ZK_EVT_RETRY_TICK;
    }
    if (strcmp(prev->time_line, now->time_line) != 0) {
        ctx->tile_a = now->run_tile;
        return ZK_EVT_MINUTE_TICK;
    }
    return -1;
}

static void log_dirty(int ev, const zk_repaint_ctx_t *ctx)
{
    zk_rect rects[16];
    int n;
    int i;
    const char *env;
    if (ev < 0) {
        return;
    }
    env = getenv("ZK_DIRTY_LOG");
    if (!env || !env[0] || strcmp(env, "0") == 0) {
        return;
    }
    zk_repaint_log((zk_repaint_event_t)ev, ctx);
    if (!g_disp) {
        return;
    }
    n = zk_repaint_rects((zk_repaint_event_t)ev, ctx, rects, 16);
    for (i = 0; i < n; i++) {
        lv_area_t a;
        if (rects[i].w <= 0 || rects[i].h <= 0) {
            continue;
        }
        a.x1 = rects[i].x;
        a.y1 = rects[i].y;
        a.x2 = rects[i].x + rects[i].w - 1;
        a.y2 = rects[i].y + rects[i].h - 1;
        lv_inv_area(g_disp, &a);
    }
}

static void capture(frame_t *f)
{
    int i;
    memset(f, 0, sizeof *f);
    f->valid = 1;
    f->screen = (int)g_screen;
    f->unreach = g_lin.offline;
    f->phase = g_stop_hold >= 0 ? g_stop_hold : (int)g_stop.phase;
    f->paused = g_paused;
    f->style = (int)g_view.hero.style;
    f->overlap = g_view.overlap;
    f->run_tile = run_index(&g_view);
    f->minutes = g_minutes;
    f->rain_on = g_view.hero.rain;
    copy_cap(f->rain_text, sizeof f->rain_text, g_view.hero.rain_text);
    copy_cap(f->time_line, sizeof f->time_line, g_view.hero.time_line);
    f->n_stations = g_lin.n_stations;
    for (i = 0; i < g_view.n_tiles && i < ZK_MAX_SHOWN; i++) {
        copy_cap(f->slots[i], sizeof f->slots[i], g_view.tiles[i].slot);
    }
}

/* Wrapped block height at this width. 0 for empty text. */
static int text_block_h(const char *text, const lv_font_t *font, int letter_sp, int line_sp, int max_w)
{
    lv_point_t sz;
    if (!text || !text[0] || !font || max_w < 1) {
        return 0;
    }
    lv_text_get_size(&sz, text, font, letter_sp, line_sp, max_w, LV_TEXT_FLAG_NONE);
    return (int)sz.y;
}

static void apply_hero(void)
{
    const zk_hero_copy_t *h = &g_view.hero;
    uint32_t bg = COL_CREAM;
    uint32_t edge = COL_EDGE;
    uint32_t lc = COL_LABEL;
    uint32_t hc = COL_INK;
    uint32_t sc = COL_MUTED;
    uint32_t tc = COL_TEXT;
    char fitted[192];
    char broken[384];
    const char *head_txt;
    int clamped;
    int head_h;
    int head_x;
    int head_w;
    int y;
    int sub_x;
    int sub_w;
    int sub_h;
    int label_lines;
    int label_h;
    int label_ls;
    int label_lh;
    int time_on;
    int time_h;
    int time_lh;
    int sub_on;
    int gap;
    int lead;
    int line_sp;
    int rain_top;
    const lv_font_t *hf = &zk_font_lit_48;
    const lv_font_t *sf;

    g_rain_yield = 0;

    switch (h->style) {
    case ZK_HERO_PAUSED:
        bg = COL_PLUM;
        edge = COL_PLUM;
        lc = COL_PLUM_MUT;
        hc = COL_PLUM_INK;
        sc = COL_PLUM_MUT;
        tc = COL_PLUM_INK;
        break;
    case ZK_HERO_OFFLINE:
    case ZK_HERO_STOP_PENDING:
        edge = COL_ERR;
        lc = COL_ERR;
        break;
    case ZK_HERO_OVERLAP:
        edge = COL_TERRA;
        lc = COL_TERRA_INK;
        sc = COL_TERRA_INK;
        break;
    case ZK_HERO_WATERING:
    case ZK_HERO_MANUAL:
    case ZK_HERO_STOP_CONFIRMED:
        lc = COL_TEAL_INK;
        break;
    case ZK_HERO_IDLE:
        lc = COL_TERRA_INK;
        break;
    default:
        break;
    }
    show(g_w.hero, !top_covered());
    paint_box(g_w.hero, bg, edge, 2, 16, LV_BORDER_SIDE_FULL);
    /* CSS label tracking is 1.4 px. 2 px is what wraps "WEEKDAYS AM" onto
     * the second line inside the 332 px slot, matching the spec screens. */
    use_font(g_w.h_label, &zk_font_sb_18, 2);
    text_color(g_w.h_label, lc);
    clamped = fit_text(fitted, sizeof fitted, h->label, &zk_font_sb_18, 2, 332, 2);
    set_tracked(g_w.h_label, fitted, clamped);
    {
        int label_w = (int)lv_text_get_width(fitted, (uint32_t)strlen(fitted), &zk_font_sb_18, 2);
        label_lines = label_w > 332 ? 2 : 1;
    }
    label_ls = 4;
    label_lh = (int)lv_font_get_line_height(&zk_font_sb_18);
    /* 22 px is the label line box used on every screen that fits (18 px face + 4 px gap). */
    label_h = label_lines * 22;
    time_lh = (int)lv_font_get_line_height(&zk_font_sb_36);
    time_h = 40;

    /* Headline box stays inset 22 / width 332. A name that needs a fourth
     * line is ellipsized at three. The box does not grow into the hero.
     * An explicit newline is the line break. This Literata 48 draws
     * "confirmed yet" at 334 px, so that authored line may use up to 16 px
     * more and still ends inside the hero. */
    head_x = 22;
    head_w = 332;
    if (h->headline[0] && strchr(h->headline, '\n')) {
        const char *p = h->headline;
        int need = 0;
        while (*p) {
            const char *nl = strchr(p, '\n');
            uint32_t n = nl ? (uint32_t)(nl - p) : (uint32_t)strlen(p);
            int lw = (int)lv_text_get_width(p, n, hf, 0);
            if (lw > need) {
                need = lw;
            }
            if (!nl) {
                break;
            }
            p = nl + 1;
        }
        if (need > head_w && need <= 332 + 16) {
            head_w = need;
        }
    }
    clamped = fit_wrapped(broken, sizeof broken, h->headline, hf, head_w, head_w, 3, NULL);
    head_txt = broken[0] ? broken : (h->headline[0] ? h->headline : "");
    use_font(g_w.h_head, hf, 0);
    text_color(g_w.h_head, hc);
    set_tracked(g_w.h_head, head_txt, clamped);

    sub_x = 22;
    sub_w = 332;
    sub_on = h->secondary[0] ? 1 : 0;
    if (h->style == ZK_HERO_OVERLAP) {
        sub_x = 48;
        sub_w = 306;
        sf = &zk_font_sb_24;
    } else {
        sf = &zk_font_r_26;
    }
    use_font(g_w.h_sub, sf, 0);
    text_color(g_w.h_sub, sc);
    set_tracked(g_w.h_sub, h->secondary, 0);
    sub_h = (int)lv_font_get_line_height(sf);
    if (sub_on) {
        lv_point_t sz;
        lv_text_get_size(&sz, h->secondary, sf, 0, 0, sub_w, LV_TEXT_FLAG_NONE);
        if ((int)sz.y > sub_h) {
            sub_h = (int)sz.y;
        }
    }
    if (sub_h < 26) {
        sub_h = 26;
    }

    time_on = h->time_line[0] ? 1 : 0;
    if (time_on) {
        use_font(g_w.h_time, &zk_font_sb_36, 0);
        text_color(g_w.h_time, tc);
        set_tracked(g_w.h_time, h->time_line, 0);
    }

    /* Rain stays at screen y 264. Children sit inside the hero's 2 px border,
     * so the local limit is 264 - (hero y + border). When the block crosses
     * that line, the rain meter yields before the ends/next line (secondary).
     * The secondary is dropped only if the hero itself cannot hold it.
     * The headline box is never shorter than its text. */
    rain_top = ZK_RAIN_Y - (ZK_HERO_Y + 2);
    lead = 9;
    gap = 8;
    line_sp = 6;
    {
        int label_gap = 8;
        int guard = 0;
        int hero_bottom = ZK_HERO_H - 4;
        while (guard < 8) {
            int bottom;
            int y_time;
            int y_sub;
            int limit;
            y = 18 + label_h + label_gap + lead;
            lv_obj_set_style_text_line_space(g_w.h_head, line_sp, 0);
            head_h = text_block_h(head_txt && head_txt[0] ? head_txt : " ", hf, 0, line_sp, head_w);
            if (head_h < 52) {
                head_h = 52;
            }
            y_time = y + head_h + gap;
            y_sub = time_on ? y_time + time_h + gap : y + head_h + gap;
            bottom = y + head_h;
            if (time_on) {
                bottom = y_time + time_h;
            }
            if (sub_on) {
                bottom = y_sub + sub_h;
            }
            limit = (h->rain && !g_rain_yield) ? rain_top : hero_bottom;
            if (bottom <= limit) {
                break;
            }
            if (lead != 0 || line_sp != 0 || gap > 4 || label_gap != 0) {
                lead = 0;
                line_sp = 0;
                gap = 4;
                label_gap = 0;
                guard++;
                continue;
            }
            if (h->rain && !g_rain_yield) {
                /* Rain yields before the ends/next line. */
                g_rain_yield = 1;
                guard++;
                continue;
            }
            if (sub_on) {
                sub_on = 0;
                guard++;
                continue;
            }
            if (label_ls != 0 || gap != 0 || time_h != time_lh) {
                label_ls = 0;
                label_h = label_lines * (label_lh > 0 ? label_lh : 18);
                if (label_h < 1) {
                    label_h = 1;
                }
                time_h = time_lh > 0 ? time_lh : 36;
                gap = 0;
                guard++;
                continue;
            }
            /* More than three lines (a run of 40 M's). Keep whole lines that
             * fit above the time. The label still holds the full string. */
            {
                int y0 = 18 + label_h + label_gap + lead;
                int room = ((h->rain && !g_rain_yield) ? rain_top : hero_bottom) - y0;
                int lh = (int)lv_font_get_line_height(hf);
                int lines;
                int whole;
                if (time_on) {
                    room -= time_h + gap;
                }
                if (lh < 1) {
                    lh = 52;
                }
                if (room < 1) {
                    room = 1;
                }
                lines = room / lh;
                if (lines < 1) {
                    head_h = room;
                } else {
                    whole = lines * lh;
                    if (line_sp > 0) {
                        whole += (lines - 1) * line_sp;
                    }
                    if (whole > room || head_h > whole) {
                        head_h = whole > room ? room : whole;
                    }
                }
            }
            break;
        }
    }
    lv_obj_set_style_text_line_space(g_w.h_label, label_ls, 0);
    place(g_w.h_label, 22, 18, 332, label_h);
    place(g_w.h_head, head_x, y, head_w, head_h);
    y = y + head_h + gap;
    if (time_on) {
        place(g_w.h_time, 22, y, 332, time_h);
        show(g_w.h_time, 1);
        y += time_h + gap;
    } else {
        show(g_w.h_time, 0);
    }
    if (h->style == ZK_HERO_OVERLAP) {
        place(g_w.warn, 22, y, 22, 22);
        recolor(g_w.warn, COL_TERRA_INK);
        show(g_w.warn, sub_on);
    } else {
        show(g_w.warn, 0);
    }
    place(g_w.h_sub, sub_x, y, sub_w, sub_on ? sub_h : 26);
    show(g_w.h_sub, sub_on);
    clickable(g_w.hero, listed(ZK_TARGET_HERO));
}

static void apply_rain(void)
{
    /* Hidden when the hero needed the rain row for the ends/next line. */
    int show_it = g_view.hero.rain && !top_covered() && !g_rain_yield;
    int inner;
    int fw;
    show(g_w.rain, show_it);
    if (!show_it) {
        return;
    }
    set_tracked(g_w.rain_tx, g_view.hero.rain_text, 0);
    inner = ZK_RAIN_TRACK - 4;
    if (inner < 1) {
        inner = 1;
    }
    fw = g_view.hero.rain_fill_px * inner / ZK_RAIN_TRACK;
    if (fw < 0) {
        fw = 0;
    }
    if (fw > inner) {
        fw = inner;
    }
    place(g_w.rain_fill, 2, 2, fw > 0 ? fw : 1, 6);
    show(g_w.rain_fill, fw > 0);
}

static void apply_tiles(const zk_layout_rects *L)
{
    int i;
    int six = L->n_tiles > 4;
    const lv_font_t *nf = six ? &zk_font_lit_20 : &zk_font_lit_26;
    int lines = six ? 2 : 3;
    int sheet = top_covered();
    g_w.n_tiles = L->n_tiles;
    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        const zk_tile_copy_t *c;
        int title_w;
        int pad;
        int th;
        int y0;
        int lh;
        int gap = 6;
        int on;
        int un;
        char wrapped[256];
        int clamped;
        int nlines;
        int slot_x;
        int slot_y;
        int slot_w;
        int slot_h;
        uint32_t bg;
        uint32_t bd;
        uint32_t nc;
        uint32_t sc;
        if (i >= L->n_tiles || sheet) {
            show(g_w.tile[i], 0);
            show(g_w.title_ln[i], 0);
            show(g_w.title_rest[i], 0);
            continue;
        }
        c = &g_view.tiles[i];
        on = c->vis == ZK_TILE_ON;
        un = c->vis == ZK_TILE_UNAVAILABLE;
        bg = on ? COL_TEAL : (un ? COL_DIM : COL_TILE);
        bd = on ? COL_TEAL : (un ? COL_DIM : COL_LINE);
        show(g_w.tile[i], 1);
        place(g_w.tile[i], L->tiles[i].x, L->tiles[i].y, L->tiles[i].w, L->tiles[i].h);
        paint_box(g_w.tile[i], bg, bd, 2, 16, LV_BORDER_SIDE_FULL);
        clickable(g_w.tile[i], listed((enum zk_target_id)(ZK_TARGET_TILE0 + i)));

        title_w = L->tiles[i].w - 28;
        if (title_w < 40) {
            title_w = 40;
        }
        /* Drop is inline on line 1 only (22 px + 6 px gap). Later lines use the full width. */
        pad = on ? 28 : 0;
        use_font(g_w.title[i], nf, 0);
        use_font(g_w.title_ln[i], nf, 0);
        use_font(g_w.title_rest[i], nf, 0);
        nc = on ? COL_WHITE : COL_INK;
        if (un) {
            nc = COL_MUTED;
        }
        text_color(g_w.title[i], nc);
        text_color(g_w.title_ln[i], nc);
        text_color(g_w.title_rest[i], nc);
        lh = (int)lv_font_get_line_height(nf);
        slot_x = L->slots[i].x - L->tiles[i].x;
        slot_y = L->slots[i].y - L->tiles[i].y;
        slot_w = L->slots[i].w;
        slot_h = L->slots[i].h;
        if (slot_w <= 0 || slot_h <= 0) {
            slot_x = ZK_SLOT_INSET;
            slot_y = six ? (ZK_SLOT_Y6 - ZK_GRID_Y) : ZK_SLOT_DY4;
            slot_w = ZK_SLOT_W;
            slot_h = ZK_SLOT_H;
        }
        /* LVGL places children inside the 2 px border. Shift so the slot's
         * screen rect is the layout slot (tile x+14, 156x24), not after the name. */
        slot_x -= 2;
        slot_y -= 2;
        if (slot_x < 0) {
            slot_x = 0;
        }
        if (slot_y < 0) {
            slot_y = 0;
        }
        {
            int first_w = title_w - pad;
            int rest_w = title_w;
            int name_bottom = slot_y - gap;
            int max_fit;
            int hang;
            const char *nl;
            if (first_w < 40) {
                first_w = 40;
            }
            if (name_bottom < lh) {
                name_bottom = lh;
            }
            {
                /* Spec clamp: 3 lines (4 stations) or 2 lines (6). A name that
                 * needs more lines than fit above the slot pushes the slot down,
                 * as the spec screens do, but never past the tile's inner bottom.
                 * The shift depends only on the name, so soil on/off never moves it. */
                const int top_min = 6;
                int inner_h = L->tiles[i].h - 4;
                int slot_max = inner_h - slot_h - 4;
                int room = (slot_max > slot_y ? slot_max : slot_y) - gap - top_min;
                max_fit = room / lh;
                if (max_fit < 1) {
                    max_fit = 1;
                }
                if (max_fit > lines) {
                    max_fit = lines;
                }
                clamped = fit_wrapped(wrapped, sizeof wrapped, c->name, nf, first_w, rest_w, max_fit, &nlines);
                if (nlines < 1) {
                    nlines = 1;
                }
                if (nlines > max_fit) {
                    nlines = max_fit;
                }
                th = lh * nlines;
                /* Bottom-align against the fixed slot so a one-line name stays put. */
                y0 = name_bottom - th;
                if (y0 < top_min) {
                    int shift = top_min - y0;
                    y0 = top_min;
                    slot_y += shift;
                    if (slot_y > slot_max && slot_max > 0) {
                        slot_y = slot_max;
                    }
                }
            }
            set_tracked(g_w.title[i], wrapped[0] ? wrapped : " ", clamped);
            nl = strchr(wrapped, '\n');
            hang = on && nl && nl[1];
            lv_obj_set_style_pad_left(g_w.title[i], hang ? 0 : pad, 0);
            lv_label_set_long_mode(g_w.title[i], LV_LABEL_LONG_WRAP);
            place(g_w.title[i], 14, y0, title_w, th);
            show(g_w.title[i], hang ? 0 : 1);
            if (hang) {
                char line1[ZK_TITLE_MAX];
                copy_span(line1, sizeof line1, wrapped, (uint32_t)(nl - wrapped));
                set_tracked(g_w.title_ln[i], line1[0] ? line1 : " ", 0);
                set_tracked(g_w.title_rest[i], nl + 1, clamped);
                place(g_w.title_ln[i], 14 + pad, y0, first_w, lh);
                place(g_w.title_rest[i], 14, y0 + lh, rest_w, th > lh ? th - lh : lh);
                show(g_w.title_ln[i], 1);
                show(g_w.title_rest[i], 1);
            } else {
                show(g_w.title_ln[i], 0);
                show(g_w.title_rest[i], 0);
            }
        }
        if (on) {
            place(g_w.drop[i], 14, y0 + (lh > 22 ? (lh - 22) / 2 : 0), 22, 22);
            recolor(g_w.drop[i], COL_WHITE);
            show(g_w.drop[i], 1);
        } else {
            show(g_w.drop[i], 0);
        }

        place(g_w.slot[i], slot_x, slot_y, slot_w, slot_h);
        show(g_w.slot[i], c->slot[0] ? 1 : 0);
        sc = on ? COL_WHITE : (c->muted_slot || un ? COL_MUTED : COL_TEXT);
        if (c->soil == ZK_SOIL_VALUE && !on) {
            use_font(g_w.slot_tx[i], &zk_font_sb_18, 0);
        } else if (c->soil == ZK_SOIL_NONE || c->soil == ZK_SOIL_STALE) {
            use_font(g_w.slot_tx[i], &zk_font_m_18, 0);
        } else {
            use_font(g_w.slot_tx[i], &zk_font_r_20, 0);
        }
        text_color(g_w.slot_tx[i], sc);
        set_tracked(g_w.slot_tx[i], c->slot, 0);
        if (c->soil == ZK_SOIL_NONE) {
            int bar_w = 20;
            int text_gap = 4;
            int tw = text_width(g_w.slot_tx[i]);
            int tx;
            int lw;
            if (tw < 0) {
                tw = 0;
            }
            /* The words stay fully inside the 156 px slot. Shrink the gap, then the empty bar. */
            while (bar_w + text_gap + tw > slot_w && text_gap > 0) {
                text_gap--;
            }
            while (bar_w + text_gap + tw > slot_w && bar_w > 0) {
                bar_w--;
            }
            tx = bar_w + text_gap;
            lw = tw > 0 ? tw : slot_w - tx;
            if (tx >= slot_w) {
                tx = 0;
                lw = slot_w;
            } else if (lw > slot_w - tx) {
                lw = slot_w - tx;
            }
            paint_box(g_w.bar[i], COL_CREAM, on ? COL_WHITE : COL_LINE, 2, 2, LV_BORDER_SIDE_FULL);
            lv_obj_set_style_bg_opa(g_w.bar[i], LV_OPA_TRANSP, 0);
            place(g_w.bar[i], 0, 7, bar_w > 0 ? bar_w : 1, 10);
            show(g_w.bar[i], bar_w > 0);
            show(g_w.fill[i], 0);
            place(g_w.slot_tx[i], tx, 3, lw, 18);
        } else if (c->soil == ZK_SOIL_VALUE || c->soil == ZK_SOIL_STALE) {
            int bw = 56;
            int fw = c->percent < 0 ? 0 : c->percent * bw / 100;
            uint32_t track = on ? COL_ON_TRACK : COL_TRACK;
            uint32_t fill = on ? COL_ON_FILL : (c->soil == ZK_SOIL_STALE ? COL_STALE : COL_TEAL);
            paint_box(g_w.bar[i], track, track, 0, 4, LV_BORDER_SIDE_NONE);
            place(g_w.bar[i], 0, 8, bw, 8);
            show(g_w.bar[i], 1);
            paint_box(g_w.fill[i], fill, fill, 0, 4, LV_BORDER_SIDE_NONE);
            place(g_w.fill[i], 0, 0, fw > 0 ? fw : 1, 8);
            show(g_w.fill[i], fw > 0);
            place(g_w.slot_tx[i], bw + 8, 2, slot_w - bw - 8, 20);
        } else {
            show(g_w.bar[i], 0);
            show(g_w.fill[i], 0);
            place(g_w.slot_tx[i], 0, 2, slot_w, 20);
        }
    }
}

static void apply_bar(const zk_layout_rects *L)
{
    int full = sheet_kind_of(g_screen) == ZK_SHEET_STOP_ALL;
    int resume = g_view.bar_is_resume;
    int dis = g_view.bar_disabled;
    uint32_t bg;
    uint32_t fg;
    uint32_t bd;
    const lv_font_t *font;
    const void *icon;
    int iw;
    int ih;
    show(g_w.pause, !full);
    show(g_w.stop, !full);
    show(g_w.sched_btn, !full);
    if (full) {
        return;
    }
    place(g_w.pause, L->pause.x, L->pause.y, L->pause.w, L->pause.h);
    place(g_w.stop, L->stop.x, L->stop.y, L->stop.w, L->stop.h);
    if (dis) {
        bg = COL_DIM;
        fg = COL_MUTED;
        bd = COL_DIM;
        font = &zk_font_m_26;
    } else if (resume) {
        bg = COL_TEAL;
        fg = COL_WHITE;
        bd = COL_TEAL;
        font = &zk_font_sb_26;
    } else {
        bg = COL_CREAM;
        fg = COL_INK;
        bd = COL_LINE;
        font = &zk_font_m_26;
    }
    paint_box(g_w.pause, bg, bd, 2, 16, LV_BORDER_SIDE_FULL);
    use_font(g_w.pause_tx, font, 0);
    text_color(g_w.pause_tx, fg);
    set_tracked(g_w.pause_tx, g_view.bar_word, 0);
    if (resume && !dis) {
        icon = &g_play30_dsc;
        iw = 30;
        ih = 30;
    } else {
        icon = &zk_icon_cloud_36;
        iw = 36;
        ih = 36;
    }
    lv_image_set_src(g_w.pause_ic, icon);
    recolor(g_w.pause_ic, fg);
    center_pair(g_w.pause, g_w.pause_ic, iw, ih, g_w.pause_tx, 10);
    clickable(g_w.pause, listed(resume ? ZK_TARGET_RESUME : ZK_TARGET_PAUSE));

    if (g_view.stop_filled) {
        paint_box(g_w.stop, COL_STOP, COL_BAND, 4, 16, LV_BORDER_SIDE_BOTTOM);
        fg = COL_WHITE;
    } else {
        paint_box(g_w.stop, COL_CREAM, COL_STOP, 2, 16, LV_BORDER_SIDE_FULL);
        fg = COL_STOP;
    }
    use_font(g_w.stop_tx, &zk_font_b_36, 3);
    text_color(g_w.stop_tx, fg);
    set_tracked(g_w.stop_tx, ZK_WORD_STOP, 0);
    recolor(g_w.stop_ic, fg);
    center_pair(g_w.stop, g_w.stop_ic, 44, 44, g_w.stop_tx, 12);
    clickable(g_w.stop, listed(ZK_TARGET_STOP));

    {
        int on_list = g_screen == ZK_SCREEN_SCHEDULES;
        int dim = g_lin.offline && !g_lin.sched_cached && !on_list;
        const char *word = on_list ? ZK_WORD_HOME : ZK_WORD_SCHEDULES;
        const void *icon = on_list ? (const void *)&g_back28_dsc : (const void *)&g_cal28_dsc;
        place(g_w.sched_btn, L->sched.x, L->sched.y, L->sched.w, L->sched.h);
        if (dim) {
            bg = COL_DIM;
            fg = COL_MUTED;
            bd = COL_DIM;
        } else {
            bg = COL_CREAM;
            fg = COL_INK;
            bd = COL_LINE;
        }
        paint_box(g_w.sched_btn, bg, bd, 2, 16, LV_BORDER_SIDE_FULL);
        use_font(g_w.sched_tx, &zk_font_r_22, 0);
        text_color(g_w.sched_tx, fg);
        set_tracked(g_w.sched_tx, word, 0);
        lv_image_set_src(g_w.sched_ic, icon);
        recolor(g_w.sched_ic, fg);
        stack_pair(g_w.sched_btn, g_w.sched_ic, 28, 28, g_w.sched_tx, 4);
        clickable(g_w.sched_btn, listed(on_list ? ZK_TARGET_HOME : ZK_TARGET_SCHEDULES));
    }
}

static void rel(const zk_rect *sheet, const zk_rect *r, lv_obj_t *o)
{
    if (!o || r->w <= 0) {
        return;
    }
    /* Children sit inside the 2 px border. Subtract it so the outer rect
     * matches the layout hit rect. */
    place(o, r->x - sheet->x - 2, r->y - sheet->y - 2, r->w, r->h);
}

static void sheet_at(const zk_rect *sheet, int x, int y, int w, int h, lv_obj_t *o)
{
    zk_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    rel(sheet, &r, o);
}

static void paint_bar(lv_obj_t *o, int x, int y, int w, int h, uint32_t col)
{
    if (!o) {
        return;
    }
    place(o, x, y, w, h);
    paint_box(o, col, col, 0, 0, LV_BORDER_SIDE_NONE);
    show(o, 1);
}

static void apply_sheet(const zk_layout_rects *L)
{
    int kind = sheet_kind_of(g_screen);
    int full = kind == ZK_SHEET_STOP_ALL;
    int run = kind == ZK_SHEET_RUN || kind == ZK_SHEET_ANYWAY;
    uint32_t edge = kind == ZK_SHEET_OVERLAP ? COL_TERRA : COL_EDGE;
    uint32_t lc = COL_LABEL;
    char value[32];
    if (kind == ZK_SHEET_NONE) {
        show(g_w.sheet, 0);
        return;
    }
    show(g_w.sheet, 1);
    place(g_w.sheet, L->sheet.x, L->sheet.y, L->sheet.w, L->sheet.h);
    paint_box(g_w.sheet, COL_CREAM, edge, 2, full ? 24 : 16, LV_BORDER_SIDE_FULL);
    clickable(g_w.sheet, 1);
    if (kind == ZK_SHEET_ANYWAY) {
        lc = COL_PLUM;
    } else if (kind == ZK_SHEET_STOP_ALL && g_lin.offline) {
        lc = COL_ERR;
    } else if (kind == ZK_SHEET_OVERLAP) {
        lc = COL_TERRA_INK;
    }
    show(g_w.s_time, kind == ZK_SHEET_STOP_STATION);
    if (kind == ZK_SHEET_STOP_STATION) {
        char fitted[240];
        int nlines = 0;
        int ell;
        use_font(g_w.s_label, &zk_font_sb_18, 1);
        text_color(g_w.s_label, lc);
        set_tracked(g_w.s_label, g_view.sheet.label, 0);
        sheet_at(&L->sheet, 36, 34, 728, 22, g_w.s_label);
        ell = fit_wrapped(fitted, sizeof fitted, g_view.sheet.title, &zk_font_lit_32, 728, 728, 2, &nlines);
        use_font(g_w.s_title, &zk_font_lit_32, 0);
        text_color(g_w.s_title, COL_INK);
        set_tracked(g_w.s_title, fitted[0] ? fitted : g_view.sheet.title, ell);
        sheet_at(&L->sheet, 36, 62, 728, 80, g_w.s_title);
        use_font(g_w.s_time, &zk_font_sb_36, 0);
        text_color(g_w.s_time, COL_TEXT);
        set_tracked(g_w.s_time, g_view.sheet.time_line, 0);
        sheet_at(&L->sheet, 36, 150, 728, 40, g_w.s_time);
        use_font(g_w.s_body, &zk_font_r_22, 0);
        text_color(g_w.s_body, COL_MUTED);
        set_tracked(g_w.s_body, g_view.sheet.body, 0);
        sheet_at(&L->sheet, 36, 196, 728, 28, g_w.s_body);
    } else {
        use_font(g_w.s_label, &zk_font_sb_18, 1);
        text_color(g_w.s_label, lc);
        set_tracked(g_w.s_label, g_view.sheet.label, 0);
        place(g_w.s_label, full ? 24 : 20, full ? 22 : 18, 728, 28);

        use_font(g_w.s_title, full ? &zk_font_lit_48 : &zk_font_lit_32, 0);
        text_color(g_w.s_title, COL_INK);
        set_tracked(g_w.s_title, g_view.sheet.title, 0);
        place(g_w.s_title, full ? 24 : 20, full ? 52 : 46, 728, full ? 70 : 80);

        use_font(g_w.s_body, full ? &zk_font_r_24 : &zk_font_r_22, 0);
        text_color(g_w.s_body, COL_MUTED);
        set_tracked(g_w.s_body, g_view.sheet.body, 0);
        /* Overlap title is one line, so the body starts at sheet-y+100. */
        place(g_w.s_body, full ? 24 : 20, kind == ZK_SHEET_OVERLAP ? 100 : (full ? 126 : 132), 728,
              kind == ZK_SHEET_OVERLAP ? 90 : (full ? 120 : 72));
    }

    show(g_w.minus, run);
    show(g_w.plus, run);
    show(g_w.value_box, run);
    show(g_w.value, run);
    show(g_w.run, run);
    show(g_w.stop_all, full);
    show(g_w.ok, kind == ZK_SHEET_OVERLAP || kind == ZK_SHEET_STOP_STATION);
    show(g_w.cancel, run || full);
    show(g_w.minus_tx, 0);
    show(g_w.plus_tx, 0);
    if (!run) {
        show(g_w.minus_bar, 0);
        show(g_w.plus_h, 0);
        show(g_w.plus_v, 0);
    }

    if (run) {
        int minus_on = g_view.sheet.minus_enabled;
        uint32_t mc = minus_on ? COL_INK : COL_MUTED;
        rel(&L->sheet, &L->minus, g_w.minus);
        rel(&L->sheet, &L->plus, g_w.plus);
        rel(&L->sheet, &L->value, g_w.value_box);
        rel(&L->sheet, &L->cancel, g_w.cancel);
        rel(&L->sheet, &L->run, g_w.run);
        paint_box(g_w.minus, minus_on ? COL_CREAM : COL_DIM, minus_on ? COL_LINE : COL_DIM, 2, 16, LV_BORDER_SIDE_FULL);
        /* Flat bars, centred in the 92 px content box inside the 2 px border. */
        paint_bar(g_w.minus_bar, 37, 44, 18, 4, mc);
        clickable(g_w.minus, listed(ZK_TARGET_MINUS));
        paint_box(g_w.plus, COL_CREAM, COL_LINE, 2, 16, LV_BORDER_SIDE_FULL);
        paint_bar(g_w.plus_h, 37, 44, 18, 4, COL_INK);
        paint_bar(g_w.plus_v, 44, 37, 4, 18, COL_INK);
        clickable(g_w.plus, listed(ZK_TARGET_PLUS));
        snprintf(value, sizeof value, "%d min", g_view.sheet.minutes);
        set_tracked(g_w.value, value, 0);
        center_pair(g_w.value_box, NULL, 0, 0, g_w.value, 0);
        paint_box(g_w.run, COL_TEAL, COL_TEAL, 2, 16, LV_BORDER_SIDE_FULL);
        text_color(g_w.run_tx, COL_WHITE);
        set_tracked(g_w.run_tx, ZK_WORD_RUN, 0);
        recolor(g_w.run_ic, COL_WHITE);
        center_pair(g_w.run, g_w.run_ic, 28, 28, g_w.run_tx, 8);
        clickable(g_w.run, listed(ZK_TARGET_RUN));
    }
    if (full) {
        rel(&L->sheet, &L->cancel, g_w.cancel);
        rel(&L->sheet, &L->stop_all, g_w.stop_all);
        paint_box(g_w.stop_all, COL_STOP, COL_BAND, 4, 16, LV_BORDER_SIDE_BOTTOM);
        text_color(g_w.stop_all_tx, COL_WHITE);
        use_font(g_w.stop_all_tx, &zk_font_b_30, 1);
        set_tracked(g_w.stop_all_tx, ZK_WORD_STOP_ALL, 0);
        recolor(g_w.stop_all_ic, COL_WHITE);
        center_pair(g_w.stop_all, g_w.stop_all_ic, 40, 40, g_w.stop_all_tx, 10);
        clickable(g_w.stop_all, listed(ZK_TARGET_STOP_ALL));
        use_font(g_w.cancel_tx, &zk_font_m_28, 0);
    } else {
        use_font(g_w.cancel_tx, &zk_font_m_26, 0);
    }
    if (run || full) {
        paint_box(g_w.cancel, COL_CREAM, COL_LINE, 2, 16, LV_BORDER_SIDE_FULL);
        text_color(g_w.cancel_tx, COL_INK);
        set_tracked(g_w.cancel_tx, ZK_WORD_CANCEL, 0);
        center_pair(g_w.cancel, NULL, 0, 0, g_w.cancel_tx, 0);
        clickable(g_w.cancel, listed(ZK_TARGET_CANCEL));
    }
    if (kind == ZK_SHEET_OVERLAP || kind == ZK_SHEET_STOP_STATION) {
        rel(&L->sheet, &L->ok, g_w.ok);
        paint_box(g_w.ok, COL_CREAM, COL_LINE, 2, 16, LV_BORDER_SIDE_FULL);
        use_font(g_w.ok_tx, &zk_font_m_26, 0);
        text_color(g_w.ok_tx, COL_INK);
        set_tracked(g_w.ok_tx, ZK_WORD_OK, 0);
        center_pair(g_w.ok, NULL, 0, 0, g_w.ok_tx, 0);
        clickable(g_w.ok, listed(ZK_TARGET_OK));
    }
}

static void apply_sched(const zk_layout_rects *L)
{
    const zk_sched_copy_t *sc = &g_view.sched;
    int on = g_screen == ZK_SCREEN_SCHEDULES;
    int i;
    static const int name_y[2] = {48, 118};
    static const int meta_y[2] = {88, 158};
    place(g_w.sched, ZK_SHEET_X, ZK_SHEET_Y, ZK_SHEET_W, ZK_SHEET_H);
    /* Next is a screen sibling, not a sheet child: the sheet border would inset it by 2 px. */
    if (L->next.w > 0) {
        place(g_w.next, L->next.x, L->next.y, L->next.w, L->next.h);
    }
    show(g_w.sched, on);
    if (!on) {
        show(g_w.next, 0);
        return;
    }
    paint_box(g_w.sched, COL_CREAM, COL_LINE, 2, 16, LV_BORDER_SIDE_FULL);
    clickable(g_w.sched, 1);
    use_font(g_w.sc_label, &zk_font_sb_18, 1);
    text_color(g_w.sc_label, COL_LABEL);
    set_tracked(g_w.sc_label, sc->label, 0);
    place(g_w.sc_label, 20, 18, 728, 22);
    show(g_w.sc_label, 1);
    for (i = 0; i < 2; i++) {
        int row = i < sc->n_rows && sc->rows[i].on;
        show(g_w.sc_name[i], row);
        show(g_w.sc_meta[i], row);
        if (!row) {
            continue;
        }
        use_font(g_w.sc_name[i], &zk_font_lit_32, 0);
        text_color(g_w.sc_name[i], sc->rows[i].enabled ? COL_INK : COL_MUTED);
        set_tracked(g_w.sc_name[i], sc->rows[i].name, 0);
        place(g_w.sc_name[i], 20, name_y[i], 728, 36);
        use_font(g_w.sc_meta[i], &zk_font_r_22, 0);
        text_color(g_w.sc_meta[i], COL_MUTED);
        set_tracked(g_w.sc_meta[i], sc->rows[i].meta, 0);
        place(g_w.sc_meta[i], 20, meta_y[i], 728, 22);
    }
    show(g_w.sc_empty, sc->n_rows == 0);
    show(g_w.sc_empty_body, sc->n_rows == 0);
    if (sc->n_rows == 0) {
        use_font(g_w.sc_empty, &zk_font_lit_32, 0);
        text_color(g_w.sc_empty, COL_INK);
        set_tracked(g_w.sc_empty, sc->empty_title, 0);
        place(g_w.sc_empty, 20, 48, 728, 36);
        use_font(g_w.sc_empty_body, &zk_font_r_22, 0);
        text_color(g_w.sc_empty_body, COL_MUTED);
        set_tracked(g_w.sc_empty_body, sc->empty_body, 0);
        place(g_w.sc_empty_body, 20, 88, 728, 22);
    }
    use_font(g_w.sc_note, &zk_font_r_22, 0);
    text_color(g_w.sc_note, COL_MUTED);
    set_tracked(g_w.sc_note, sc->note, 0);
    place(g_w.sc_note, 20, 188, 728, 22);
    show(g_w.sc_note, 1);
    show(g_w.next, sc->next_on);
    if (sc->next_on) {
        paint_box(g_w.next, COL_CREAM, COL_LINE, 2, 16, LV_BORDER_SIDE_FULL);
        use_font(g_w.next_tx, &zk_font_m_26, 0);
        text_color(g_w.next_tx, COL_INK);
        set_tracked(g_w.next_tx, ZK_WORD_NEXT, 0);
        center_pair(g_w.next, NULL, 0, 0, g_w.next_tx, 0);
        clickable(g_w.next, listed(ZK_TARGET_SCHED_NEXT));
    }
}

static void sync_hits(void)
{
    int i;
    memset(g_w.hit, 0, sizeof g_w.hit);
    g_w.hit[ZK_TARGET_HERO] = g_w.hero;
    g_w.hit[ZK_TARGET_PAUSE] = g_w.pause;
    g_w.hit[ZK_TARGET_RESUME] = g_w.pause;
    g_w.hit[ZK_TARGET_STOP] = g_w.stop;
    g_w.hit[ZK_TARGET_SCHEDULES] = g_w.sched_btn;
    g_w.hit[ZK_TARGET_HOME] = g_w.sched_btn;
    g_w.hit[ZK_TARGET_SCHED_NEXT] = g_w.next;
    g_w.hit[ZK_TARGET_MINUS] = g_w.minus;
    g_w.hit[ZK_TARGET_PLUS] = g_w.plus;
    g_w.hit[ZK_TARGET_CANCEL] = g_w.cancel;
    g_w.hit[ZK_TARGET_RUN] = g_w.run;
    g_w.hit[ZK_TARGET_STOP_ALL] = g_w.stop_all;
    g_w.hit[ZK_TARGET_OK] = g_w.ok;
    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        g_w.hit[ZK_TARGET_TILE0 + i] = g_w.tile[i];
    }
}

static void build_tree(lv_obj_t *scr)
{
    int i;
    blit_a8(&zk_icon_stop_oct_84, 44, 44, g_oct44, &g_oct44_dsc);
    blit_a8(&zk_icon_stop_oct_84, 40, 40, g_oct40, &g_oct40_dsc);
    blit_a8(&zk_icon_play_44, 30, 30, g_play30, &g_play30_dsc);
    blit_a8(&zk_icon_play_44, 28, 28, g_play28, &g_play28_dsc);
    blit_a8(&zk_icon_drop_30, 22, 22, g_drop22, &g_drop22_dsc);
    blit_a8(&zk_icon_list_52, 28, 28, g_cal28, &g_cal28_dsc);
    blit_a8(&zk_icon_back_44, 28, 28, g_back28, &g_back28_dsc);
    build_warn();
    tracks_reset();

    g_w.hero = make_box(scr);
    place(g_w.hero, ZK_HERO_X, ZK_HERO_Y, ZK_HERO_W, ZK_HERO_H);
    listen(g_w.hero);
    mark_lab("hero_label", 0);
    g_w.h_label = make_label(g_w.hero, &zk_font_sb_18, COL_LABEL);
    mark_lab("hero_headline", 0);
    g_w.h_head = make_label(g_w.hero, &zk_font_lit_48, COL_INK);
    mark_lab("hero_time", 0);
    g_w.h_time = make_label(g_w.hero, &zk_font_sb_36, COL_TEXT);
    mark_lab("hero_secondary", 0);
    g_w.h_sub = make_label(g_w.hero, &zk_font_r_26, COL_MUTED);
    g_w.warn = make_icon(g_w.hero, &g_warn_dsc);

    g_w.rain = make_box(scr);
    lv_obj_set_style_bg_opa(g_w.rain, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_w.rain, 0, 0);
    place(g_w.rain, ZK_RAIN_X, ZK_RAIN_Y, ZK_RAIN_W, ZK_RAIN_H);
    mark_lab("rain_text", 0);
    g_w.rain_tx = make_label(g_w.rain, &zk_font_m_20, COL_TEXT);
    place(g_w.rain_tx, 0, 0, 328, 22);
    g_w.rain_meter = make_box(g_w.rain);
    place(g_w.rain_meter, 0, 26, ZK_RAIN_TRACK, 10);
    paint_box(g_w.rain_meter, COL_CREAM, COL_TEAL_INK, 2, 5, LV_BORDER_SIDE_FULL);
    lv_obj_set_style_bg_opa(g_w.rain_meter, LV_OPA_TRANSP, 0);
    g_w.rain_fill = make_box(g_w.rain_meter);
    paint_box(g_w.rain_fill, COL_TEAL_INK, COL_TEAL_INK, 0, 2, LV_BORDER_SIDE_NONE);
    mark_lab("rain_0", 0);
    g_w.rain_0 = make_label(g_w.rain, &zk_font_m_18, COL_MUTED);
    set_tracked(g_w.rain_0, "0", 0);
    place(g_w.rain_0, 0, 40, 40, 20);
    mark_lab("rain_1", 0);
    g_w.rain_1 = make_label(g_w.rain, &zk_font_m_18, COL_MUTED);
    set_tracked(g_w.rain_1, "1 in", 0);
    place(g_w.rain_1, 268, 40, 60, 20);
    lv_obj_set_style_text_align(g_w.rain_1, LV_TEXT_ALIGN_RIGHT, 0);

    for (i = 0; i < ZK_MAX_SHOWN; i++) {
        g_w.tile[i] = make_box(scr);
        listen(g_w.tile[i]);
        mark_lab_n("tile_title", i, 0);
        g_w.title[i] = make_label(g_w.tile[i], &zk_font_lit_26, COL_INK);
        g_w.title_ln[i] = make_quiet_label(g_w.tile[i], &zk_font_lit_26, COL_INK);
        g_w.title_rest[i] = make_quiet_label(g_w.tile[i], &zk_font_lit_26, COL_INK);
        g_w.drop[i] = make_icon(g_w.tile[i], &g_drop22_dsc);
        g_w.slot[i] = make_box(g_w.tile[i]);
        lv_obj_set_style_bg_opa(g_w.slot[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_w.slot[i], 0, 0);
        g_w.bar[i] = make_box(g_w.slot[i]);
        g_w.fill[i] = make_box(g_w.bar[i]);
        mark_lab_n("tile_slot", i, 0);
        g_w.slot_tx[i] = make_label(g_w.slot[i], &zk_font_r_20, COL_MUTED);
        lv_label_set_long_mode(g_w.slot_tx[i], LV_LABEL_LONG_CLIP);
    }

    g_w.pause = make_box(scr);
    listen(g_w.pause);
    g_w.pause_ic = make_icon(g_w.pause, &zk_icon_cloud_36);
    mark_lab("pause_word", 0);
    g_w.pause_tx = make_label(g_w.pause, &zk_font_m_26, COL_INK);
    lv_label_set_long_mode(g_w.pause_tx, LV_LABEL_LONG_CLIP);

    g_w.stop = make_box(scr);
    listen(g_w.stop);
    g_w.stop_ic = make_icon(g_w.stop, &g_oct44_dsc);
    mark_lab("stop_word", 0);
    g_w.stop_tx = make_label(g_w.stop, &zk_font_b_36, COL_STOP);
    lv_label_set_long_mode(g_w.stop_tx, LV_LABEL_LONG_CLIP);

    g_w.sched_btn = make_box(scr);
    listen(g_w.sched_btn);
    g_w.sched_ic = make_icon(g_w.sched_btn, &g_cal28_dsc);
    mark_lab("sched_word", 0);
    g_w.sched_tx = make_label(g_w.sched_btn, &zk_font_r_22, COL_INK);
    lv_label_set_long_mode(g_w.sched_tx, LV_LABEL_LONG_CLIP);

    g_w.sched = make_box(scr);
    listen(g_w.sched);
    mark_lab("sched_label", 0);
    g_w.sc_label = make_label(g_w.sched, &zk_font_sb_18, COL_LABEL);
    lv_label_set_long_mode(g_w.sc_label, LV_LABEL_LONG_CLIP);
    mark_lab("sched_name0", 0);
    g_w.sc_name[0] = make_label(g_w.sched, &zk_font_lit_32, COL_INK);
    lv_label_set_long_mode(g_w.sc_name[0], LV_LABEL_LONG_CLIP);
    mark_lab("sched_meta0", 0);
    g_w.sc_meta[0] = make_label(g_w.sched, &zk_font_r_22, COL_MUTED);
    lv_label_set_long_mode(g_w.sc_meta[0], LV_LABEL_LONG_CLIP);
    mark_lab("sched_name1", 0);
    g_w.sc_name[1] = make_label(g_w.sched, &zk_font_lit_32, COL_INK);
    lv_label_set_long_mode(g_w.sc_name[1], LV_LABEL_LONG_CLIP);
    mark_lab("sched_meta1", 0);
    g_w.sc_meta[1] = make_label(g_w.sched, &zk_font_r_22, COL_MUTED);
    lv_label_set_long_mode(g_w.sc_meta[1], LV_LABEL_LONG_CLIP);
    mark_lab("sched_note", 0);
    g_w.sc_note = make_label(g_w.sched, &zk_font_r_22, COL_MUTED);
    lv_label_set_long_mode(g_w.sc_note, LV_LABEL_LONG_CLIP);
    mark_lab("sched_empty", 0);
    g_w.sc_empty = make_label(g_w.sched, &zk_font_lit_32, COL_INK);
    lv_label_set_long_mode(g_w.sc_empty, LV_LABEL_LONG_CLIP);
    mark_lab("sched_empty_body", 0);
    g_w.sc_empty_body = make_label(g_w.sched, &zk_font_r_22, COL_MUTED);
    lv_label_set_long_mode(g_w.sc_empty_body, LV_LABEL_LONG_CLIP);
    g_w.next = make_box(scr);
    listen(g_w.next);
    mark_lab("next_word", 0);
    g_w.next_tx = make_label(g_w.next, &zk_font_m_26, COL_INK);
    lv_label_set_long_mode(g_w.next_tx, LV_LABEL_LONG_CLIP);
    show(g_w.next, 0);
    show(g_w.sched, 0);

    g_w.sheet = make_box(scr);
    listen(g_w.sheet);
    mark_lab("sheet_label", 0);
    g_w.s_label = make_label(g_w.sheet, &zk_font_sb_18, COL_LABEL);
    mark_lab("sheet_title", 0);
    g_w.s_title = make_label(g_w.sheet, &zk_font_lit_32, COL_INK);
    mark_lab("sheet_time", 0);
    g_w.s_time = make_label(g_w.sheet, &zk_font_sb_36, COL_TEXT);
    mark_lab("sheet_body", 0);
    g_w.s_body = make_label(g_w.sheet, &zk_font_r_22, COL_MUTED);

    g_w.minus = make_box(g_w.sheet);
    listen(g_w.minus);
    g_w.minus_bar = make_box(g_w.minus);
    mark_lab("minus_word", 0);
    g_w.minus_tx = make_label(g_w.minus, &zk_font_m_28, COL_INK);
    g_w.plus = make_box(g_w.sheet);
    listen(g_w.plus);
    g_w.plus_h = make_box(g_w.plus);
    g_w.plus_v = make_box(g_w.plus);
    mark_lab("plus_word", 0);
    g_w.plus_tx = make_label(g_w.plus, &zk_font_m_28, COL_INK);
    g_w.value_box = make_box(g_w.sheet);
    lv_obj_set_style_bg_opa(g_w.value_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_w.value_box, 0, 0);
    lv_obj_set_style_border_opa(g_w.value_box, LV_OPA_TRANSP, 0);
    g_w.value = make_label(g_w.value_box, &zk_font_lit_40, COL_INK);
    lv_label_set_long_mode(g_w.value, LV_LABEL_LONG_CLIP);

    g_w.cancel = make_box(g_w.sheet);
    listen(g_w.cancel);
    mark_lab("cancel_word", 0);
    g_w.cancel_tx = make_label(g_w.cancel, &zk_font_m_26, COL_INK);
    g_w.run = make_box(g_w.sheet);
    listen(g_w.run);
    g_w.run_ic = make_icon(g_w.run, &g_play28_dsc);
    mark_lab("run_word", 0);
    g_w.run_tx = make_label(g_w.run, &zk_font_sb_26, COL_WHITE);
    g_w.stop_all = make_box(g_w.sheet);
    listen(g_w.stop_all);
    g_w.stop_all_ic = make_icon(g_w.stop_all, &g_oct40_dsc);
    mark_lab("stop_all_word", 0);
    g_w.stop_all_tx = make_label(g_w.stop_all, &zk_font_b_30, COL_WHITE);
    g_w.ok = make_box(g_w.sheet);
    listen(g_w.ok);
    mark_lab("ok_word", 0);
    g_w.ok_tx = make_label(g_w.ok, &zk_font_m_26, COL_INK);

    g_w.hit[ZK_TARGET_HERO] = g_w.hero;
    g_built = 1;
}

static void log_hero(void);

static void paint(void)
{
    zk_layout_rects L;
    zk_repaint_ctx_t ctx;
    frame_t now;
    int ev;
    int i;
    zk_layout_rects_of(g_screen, &g_lin, &L);
    g_ntg = zk_layout_targets(g_screen, &g_lin, g_tg, TG_MAX);
    if (g_ntg > TG_MAX) {
        g_ntg = TG_MAX;
    }
    apply_hero();
    apply_rain();
    apply_tiles(&L);
    apply_bar(&L);
    apply_sched(&L);
    apply_sheet(&L);
    sync_hits();
    for (i = 1; i < HIT_N; i++) {
        if (g_w.hit[i]) {
            lv_obj_remove_state(g_w.hit[i], LV_STATE_PRESSED);
        }
    }
    capture(&now);
    ev = pick_event(&g_prev, &now, &ctx);
    log_dirty(ev, &ctx);
    g_prev = now;
    g_retry_pulse = 0;
    g_ready = 1;
    log_hero();
}

/* One log line. A drawn break and a non-breaking space still read as the sentence. */
static void flatten_hero_log(char *s)
{
    char *r;
    char *w;
    if (!s) {
        return;
    }
    r = s;
    w = s;
    while (*r) {
        if (*r == '\n') {
            *w++ = ' ';
            r++;
            continue;
        }
        if ((unsigned char)r[0] == 0xC2 && (unsigned char)r[1] == 0xA0) {
            *w++ = ' ';
            r += 2;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

static void log_hero(void)
{
    const char *env;
    static char prev[512];
    char line[512];
    env = getenv("ZK_HERO_LOG");
    if (!env || strcmp(env, "1") != 0) {
        return;
    }
    snprintf(line, sizeof line, "HERO %s | %s", g_view.hero.headline, g_view.hero.secondary);
    flatten_hero_log(line);
    if (strcmp(prev, line) == 0) {
        return;
    }
    snprintf(prev, sizeof prev, "%s", line);
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

void zk_ui_debug_set_stop(int phase)
{
    g_stop_hold = phase;
}

void zk_ui_debug_tap(int x, int y)
{
    apply_id(zk_hit_test(g_tg, g_ntg, x, y));
    zk_ui_refresh();
}

int zk_ui_debug_label_boxes(zk_ui_label_box_t *out, int cap)
{
    lv_obj_t *scr;
    int i;
    if (g_lab_overflow || !out || cap < g_nlabs) {
        return -1;
    }
    scr = lv_screen_active();
    if (scr) {
        lv_obj_update_layout(scr);
    }
    for (i = 0; i < g_nlabs; i++) {
        zk_ui_label_box_t *b = &out[i];
        lv_obj_t *o = g_labs[i].obj;
        lv_obj_t *p;
        lv_area_t a;
        lv_area_t pa;
        const char *t;
        memset(b, 0, sizeof *b);
        copy_cap(b->id, sizeof b->id, g_labs[i].id);
        b->clamped = g_labs[i].clamped;
        if (!o) {
            continue;
        }
        b->visible = lv_obj_is_visible(o) ? 1 : 0;
        b->y_rel = (int)lv_obj_get_y(o);
        lv_obj_get_coords(o, &a);
        b->x = (int)a.x1;
        b->y = (int)a.y1;
        b->w = (int)lv_area_get_width(&a);
        b->h = (int)lv_area_get_height(&a);
        p = lv_obj_get_parent(o);
        if (p) {
            lv_obj_get_coords(p, &pa);
            b->parent_x = (int)pa.x1;
            b->parent_y = (int)pa.y1;
            b->parent_w = (int)lv_area_get_width(&pa);
            b->parent_h = (int)lv_area_get_height(&pa);
        }
        t = lv_label_get_text(o);
        copy_cap(b->text, sizeof b->text, t);
    }
    return g_nlabs;
}

int zk_ui_debug_tile(int index, zk_ui_tile_dbg_t *out)
{
    lv_obj_t *tile;
    lv_color_t bg;
    lv_color_t bd;
    if (!out) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    if (index < 0 || index >= g_w.n_tiles || index >= ZK_MAX_SHOWN) {
        return 0;
    }
    tile = g_w.tile[index];
    if (!tile || lv_obj_has_flag(tile, LV_OBJ_FLAG_HIDDEN)) {
        return 0;
    }
    out->present = 1;
    bg = lv_obj_get_style_bg_color(tile, LV_PART_MAIN);
    bd = lv_obj_get_style_border_color(tile, LV_PART_MAIN);
    out->bg = lv_color_to_u32(bg) & 0xFFFFFFu;
    out->border = lv_color_to_u32(bd) & 0xFFFFFFu;
    out->border_w = (int)lv_obj_get_style_border_width(tile, LV_PART_MAIN);
    out->on = out->bg == COL_TEAL ? 1 : 0;
    out->drop_visible = (g_w.drop[index] && lv_obj_is_visible(g_w.drop[index])) ? 1 : 0;
    if (g_w.title[index]) {
        lv_color_t tc = lv_obj_get_style_text_color(g_w.title[index], LV_PART_MAIN);
        out->title_w = (int)lv_obj_get_width(g_w.title[index]);
        out->name_rgb = lv_color_to_u32(tc) & 0xFFFFFFu;
    }
    out->tile_w = (int)lv_obj_get_width(tile);
    return 1;
}

void zk_ui_set_fixture(const char *dir)
{
    if (!dir || !dir[0]) {
        g_fix[0] = 0;
        g_app.fixture_dir = NULL;
        return;
    }
    snprintf(g_fix, sizeof g_fix, "%s", dir);
    g_app.fixture_dir = g_fix;
}

void zk_ui_note_touch(void)
{
    g_idle_from = zk_platform_mono_ms();
    g_idle_ready = 1;
}

void zk_ui_debug_set_minutes(int minutes)
{
    g_minutes_hold = (minutes >= 1 && minutes <= 60) ? minutes : -1;
}

void zk_ui_debug_set_sched(int index)
{
    g_sched_hold = index;
}

static void fill_glyph(lv_obj_t *o, zk_ui_glyph_dbg_t *out)
{
    lv_area_t a;
    lv_color_t c;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (!o || !lv_obj_is_visible(o)) {
        return;
    }
    out->present = 1;
    lv_obj_get_coords(o, &a);
    out->x = (int)a.x1;
    out->y = (int)a.y1;
    out->w = (int)lv_area_get_width(&a);
    out->h = (int)lv_area_get_height(&a);
    c = lv_obj_get_style_bg_color(o, LV_PART_MAIN);
    out->color = lv_color_to_u32(c) & 0xFFFFFFu;
}

int zk_ui_debug_glyphs(zk_ui_glyph_dbg_t *minus, zk_ui_glyph_dbg_t *plus_h, zk_ui_glyph_dbg_t *plus_v)
{
    lv_obj_t *scr = lv_screen_active();
    if (scr) {
        lv_obj_update_layout(scr);
    }
    fill_glyph(g_w.minus_bar, minus);
    fill_glyph(g_w.plus_h, plus_h);
    fill_glyph(g_w.plus_v, plus_v);
    return 1;
}

void zk_ui_debug_set_view(int screen, int pressed_target, int pick_chip)
{
    g_override = screen;
    g_pressed = pressed_target;
    g_minutes_hold = -1;
    g_sched_hold = -1;
    if (pick_chip >= 0) {
        g_station = pick_chip;
    }
    if (screen >= 0) {
        g_screen = (enum zk_screen)screen;
    } else {
        g_screen = ZK_SCREEN_HOME;
    }
    if (g_screen == ZK_SCREEN_SCHEDULES) {
        g_sched_i = 0;
    }
    g_min_key = -999;
}

static int width_r22(const char *text, size_t n, void *user)
{
    const lv_font_t *font = user ? (const lv_font_t *)user : &zk_font_r_22;
    if (!text || n == 0) {
        return 0;
    }
    return (int)lv_text_get_width(text, (uint32_t)n, font, 0);
}

void zk_ui_tick(void)
{
    zk_copy_src_t src;
    zk_view_in_t vin;
    zk_stop_phase_t phase;
    int unreachable;
    int watering;
    int manual;
    int paused;
    int key;
    lv_obj_t *scr;

    if (!g_built) {
        scr = lv_screen_active();
        if (!scr) {
            return;
        }
        build_tree(scr);
    }
    zk_data_get(&g_snap);
    unreachable = unreachable_of(&g_snap);
    watering = watering_of(&g_snap);
    manual = manual_of(&g_snap);
    paused = (g_snap.have_kiosk && g_snap.kiosk.pause.paused) ? 1 : 0;
    maybe_clear_confirmed(unreachable, watering, manual, paused);
    note_actions(&g_snap);
    advance_stop(&g_snap, unreachable, watering);
    maybe_retry();
    advance_stop(&g_snap, unreachable, watering);

    if (g_override >= 0) {
        g_screen = (enum zk_screen)g_override;
    } else if (g_stop_hold < 0) {
        int64_t now = zk_platform_mono_ms();
        int64_t limit = zk_idle_home_ms();
        if (!g_idle_ready) {
            g_idle_from = now;
            g_idle_ready = 1;
        } else if (now >= g_idle_from && zk_flow_idle_returns_home_limit(g_screen, now - g_idle_from, limit)) {
            /* Back to Home. Stop all is not confirmed. */
            g_screen = ZK_SCREEN_HOME;
            g_under = ZK_SCREEN_HOME;
            g_idle_from = now;
        }
    }
    key = (g_screen == ZK_SCREEN_SHEET_RUN || g_screen == ZK_SCREEN_SHEET_ANYWAY) ? ((int)g_screen * 100 + g_station)
                                                                                    : -1;
    memset(&src, 0, sizeof src);
    src.kiosk = g_snap.have_kiosk ? &g_snap.kiosk : NULL;
    src.have_kiosk = g_snap.have_kiosk;
    src.schedules = &g_snap.schedules;
    src.have_schedules = g_snap.have_schedules;
    src.unreachable = unreachable;
    src.wall = g_snap.wall;
    src.soil_doc = g_snap.have_soil_doc ? &g_snap.soil_doc : NULL;
    src.lasts = g_snap.lasts;
    src.n_lasts = g_snap.n_lasts;
    if (key != g_min_key) {
        g_min_key = key;
        if (key >= 0) {
            g_minutes = zk_usual_minutes(&src, g_station);
        }
    }
    if (g_minutes_hold >= 1 && g_minutes_hold <= 60) {
        g_minutes = g_minutes_hold;
    }
    phase = g_stop_hold >= 0 ? (zk_stop_phase_t)g_stop_hold : g_stop.phase;
    memset(&vin, 0, sizeof vin);
    vin.sheet_kind = sheet_kind_of(g_screen);
    vin.station_index = g_station;
    vin.minutes = g_minutes;
    vin.stop_phase = phase;
    /* Pending from the first in-flight POST, reachable or not. Copy picks the sentence. */
    vin.offline_pending = (phase == ZK_STOP_QUEUED || phase == ZK_STOP_WAIT) ? 1 : 0;
    vin.width_px = width_r22;
    vin.width_user = (void *)&zk_font_r_22;
    if (g_stop_hold < 0 && g_has_confirmed_at) {
        vin.confirmed_at = g_confirmed_at;
        vin.has_confirmed_at = 1;
    }
    vin.soil_cfg = &g_soil;
    if (g_sched_hold >= 0) {
        g_sched_i = g_sched_hold;
    }
    if (!g_snap.have_schedules || g_snap.schedules.n <= 0 || g_sched_i < 0 || g_sched_i >= g_snap.schedules.n) {
        g_sched_i = 0;
    }
    vin.sched_index = g_sched_i;
    zk_view_build(&src, &vin, &g_view);

    memset(&g_lin, 0, sizeof g_lin);
    g_lin.n_stations = g_snap.have_kiosk ? g_snap.kiosk.n_stations : 0;
    g_lin.paused = paused;
    g_lin.offline = unreachable;
    g_lin.stop_pending = g_view.hero.style == ZK_HERO_STOP_PENDING ? 1 : 0;
    g_lin.overlap = g_view.overlap;
    g_lin.minutes = g_minutes;
    g_lin.sched_cached = g_snap.have_schedules ? 1 : 0;
    g_lin.n_schedules = g_snap.have_schedules ? g_snap.schedules.n : 0;
    g_paused = paused;
    g_prev_paused = paused;
    g_prev_unreach = unreachable;
    g_have_prev_flags = 1;
    paint();
}

void zk_ui_refresh(void)
{
    int i;
    zk_ui_tick();
    for (i = 1; i < HIT_N; i++) {
        if (g_w.hit[i]) {
            lv_obj_remove_state(g_w.hit[i], LV_STATE_PRESSED);
        }
    }
    if (g_pressed > 0 && g_pressed < HIT_N && g_w.hit[g_pressed]) {
        lv_obj_add_state(g_w.hit[g_pressed], LV_STATE_PRESSED);
    }
    if (g_disp) {
        lv_refr_now(g_disp);
    }
}

void zk_ui_init(lv_display_t *disp, const zk_app_t *app)
{
    lv_obj_t *scr;
    g_disp = disp;
    memset(&g_app, 0, sizeof g_app);
    if (app) {
        g_app = *app;
        if (app->fixture_dir && app->fixture_dir[0]) {
            zk_ui_set_fixture(app->fixture_dir);
        } else {
            g_app.fixture_dir = NULL;
        }
    }
    zk_soil_cfg_load(&g_soil, NULL);
    g_override = -1;
    g_pressed = 0;
    g_screen = ZK_SCREEN_HOME;
    g_under = ZK_SCREEN_HOME;
    g_sched_i = 0;
    g_station = 0;
    g_minutes = 10;
    g_min_key = -999;
    g_ready = 0;
    g_stop_hold = -1;
    g_minutes_hold = -1;
    g_sched_hold = -1;
    g_idle_from = zk_platform_mono_ms();
    g_idle_ready = 1;
    memset(&g_stop, 0, sizeof g_stop);
    memset(&g_prev, 0, sizeof g_prev);
    scr = lv_screen_active();
    if (scr) {
        lv_obj_set_style_bg_color(scr, hex(COL_SAND), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    }
    zk_ui_refresh();
}

int zk_ui_hit_is_stop(int x, int y)
{
    if (!g_ready) {
        return 0;
    }
    return zk_layout_hit_is_stop(g_screen, &g_lin, x, y);
}

void zk_ui_power_inputs(zk_power_inputs_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (!g_snap.have_kiosk || g_snap.api_state != ZK_API_OK || g_snap.stale) {
        out->unreachable = 1;
    }
    if (g_snap.have_kiosk) {
        out->watering = watering_of(&g_snap);
        out->lockout = g_snap.kiosk.lockout ? 1 : 0;
        out->fault = (g_snap.kiosk.fault || g_snap.kiosk.last_error[0] || strcmp(g_snap.kiosk.phase, "Fault") == 0) ? 1
                                                                                                                    : 0;
        out->paused = g_snap.kiosk.pause.paused ? 1 : 0;
    }
    if (sheet_kind_of(g_screen) != ZK_SHEET_NONE) {
        out->modal_open = 1;
    }
}
