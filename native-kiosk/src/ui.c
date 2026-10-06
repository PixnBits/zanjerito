#include "ui.h"

#include "data.h"
#include "platform.h"
#include "zk_layout.h"
#include "zk_logic.h"
#include "zk_text.h"

#include "generated/zk_fonts.h"
#include "generated/zk_icons.h"

#include <stdio.h>
#include <string.h>

enum {
    COL_BG = 0xEFDDBE,
    COL_CARD = 0xFCF5E6,
    COL_INK = 0x33200F,
    COL_MUT = 0x66492F,
    COL_LINE = 0xD2B78A,
    COL_TRACK = 0xEBDCC0,
    COL_TEAL = 0x0F6E6C,
    COL_FILL = 0x2A908B,
    COL_LOW = 0xCD6A32,
    COL_LOWTX = 0xA93C14,
    COL_STOP = 0xB5361A,
    COL_STOPSH = 0x7C2411,
    COL_PAUSE = 0x4E3F63,
    COL_BACK = 0xDCC59B,
    COL_RAINBG = 0xCFE6E1,
    COL_RAINFG = 0x0B4A48,
    COL_RUNBG = 0x0F5E5C,
    COL_RUNLBL = 0xBFEDE6,
    COL_RUNFILL = 0xF2C777,
    COL_WHITE = 0xFFFFFF,
    COL_STALE_BG = 0xF6D5CC,
    COL_DIM = 0x33200F
};

#define MIDDOT "\xC2\xB7"
#define ELLIPSIS ZK_TEXT_ELLIPSIS

enum { HIT_N = 32, ACT_SCHED_BODY = 1000 };

typedef struct {
    int content;
    int modal;
    int rain;
    int fault;
    int n_st;
    int soil_mask;
    int steps;
    int sched_i;
    int station_i;
    int have;
} view_key;

typedef struct {
    lv_obj_t *hit[HIT_N];
    lv_obj_t *shadow;
    lv_obj_t *hdr_kicker;
    lv_obj_t *hdr_big;
    lv_obj_t *rain_bold;
    lv_obj_t *rain_rest;
    lv_obj_t *fault_lbl;
    lv_obj_t *tile_title[8];
    lv_obj_t *tile_pct[8];
    lv_obj_t *tile_fill[8];
    lv_obj_t *tile_pill[8];
    int tile_bar_w[8];
    int n_tiles;
    lv_obj_t *run_title;
    lv_obj_t *run_count;
    lv_obj_t *run_fill;
    lv_obj_t *run_step;
    lv_obj_t *run_next;
    int run_bar_w;
    lv_obj_t *ban_title;
    lv_obj_t *ban_until;
    lv_obj_t *info_name;
    lv_obj_t *info_sub0;
    lv_obj_t *info_sub1;
    lv_obj_t *chip_t[4];
    lv_obj_t *chip_s[4];
    lv_obj_t *sched_count;
    lv_obj_t *sched_name;
    lv_obj_t *sched_sum;
    lv_obj_t *sched_ends1;
    lv_obj_t *sched_ends2;
    lv_obj_t *row_name[5];
    lv_obj_t *row_min[5];
    lv_obj_t *row_unit[5];
    lv_obj_t *hold_face;
    int n_rows;
    lv_obj_t *m_title;
    lv_obj_t *m_body;
    lv_obj_t *m_sub;
    lv_obj_t *stale;
    lv_obj_t *toast;
    lv_obj_t *toast_lbl;
    lv_obj_t *st_title;
    lv_obj_t *st_dot;
    lv_obj_t *st_state;
    lv_obj_t *st_exempt;
    lv_obj_t *st_soil_pct;
    lv_obj_t *st_soil_fill;
    lv_obj_t *st_soil_msg;
    int st_bar_w;
    lv_obj_t *nu_title;
    lv_obj_t *nu_body;
} widgets_t;

static zk_app_t g_app;
static char g_fix[512];
static lv_display_t *g_disp;
static widgets_t g_w;
static view_key g_key;
static int g_have_key;
static zk_layout_in g_lin;
static int g_lin_ready;
static int g_hit_content;
static int g_hit_modal;
static int g_dirty;
static int g_override = -1;
static int g_nav = -1;
static int g_modal;
static int g_pressed;
static int g_pick_chip = -1;
static int g_sched_i;
static int g_station_i;
static char g_station_id[ZK_ID_MAX];
static uint32_t g_ver;
static uint32_t g_seen_seq;
static int g_seen_ok;
static char g_toast_text[96];
static double g_toast_until;
static double g_quiet_until; /* ignore chip and modal taps until this mono time */

enum { LAB_TRACK_MAX = 96, LAB_ID_STORE = 32 };

typedef struct {
    char id[LAB_ID_STORE];
    lv_obj_t *obj;
    int clamped;
} lab_slot_t;

static lab_slot_t g_labs[LAB_TRACK_MAX];
static int g_nlabs;
static int g_lab_overflow;
static const char *g_mark_id;
static int g_mark_clamped;
static char g_idbuf[LAB_ID_STORE];

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

static void remember(lv_obj_t *lb)
{
    lab_slot_t *s;
    if (g_nlabs >= LAB_TRACK_MAX) {
        g_lab_overflow = 1;
        g_mark_id = NULL;
        g_mark_clamped = 0;
        return;
    }
    s = &g_labs[g_nlabs];
    if (g_mark_id) {
        copy_cap(s->id, sizeof s->id, g_mark_id);
    } else {
        snprintf(s->id, sizeof s->id, "l%d", g_nlabs);
    }
    s->obj = lb;
    s->clamped = g_mark_clamped;
    g_nlabs++;
    g_mark_id = NULL;
    g_mark_clamped = 0;
}

static lv_color_t hex(uint32_t c)
{
    return lv_color_hex(c);
}

static void plain(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

static void set_lab(lv_obj_t *lb, const char *text)
{
    const char *cur;
    if (!lb) {
        return;
    }
    if (!text) {
        text = "";
    }
    cur = lv_label_get_text(lb);
    if (cur && strcmp(cur, text) == 0) {
        return;
    }
    lv_label_set_text(lb, text);
}

typedef struct {
    const lv_font_t *font;
    int32_t letter_space;
} fit_ctx_t;

static int fit_width_cb(const char *text, size_t nbytes, void *user)
{
    fit_ctx_t *u = user;
    if (!text || !u || !u->font) {
        return 0;
    }
    if (nbytes > 0x7fffffffu) {
        nbytes = 0x7fffffffu;
    }
    return (int)lv_text_get_width(text, (uint32_t)nbytes, u->font, u->letter_space);
}

/* One line, clipped to max_px, with U+2026 when the source does not fit.
 * Does not change the label's width: right-aligned slots stay put. */
static void set_lab_fit_px(lv_obj_t *lb, const char *text, int max_px)
{
    char buf[512];
    fit_ctx_t u;
    const lv_font_t *font;
    int32_t h;
    if (!lb) {
        return;
    }
    font = lv_obj_get_style_text_font(lb, LV_PART_MAIN);
    u.font = font;
    u.letter_space = lv_obj_get_style_text_letter_space(lb, LV_PART_MAIN);
    if (max_px < 1 || !font || zk_text_fit(buf, sizeof buf, text, max_px, fit_width_cb, &u) != 0) {
        set_lab(lb, text);
    } else {
        set_lab(lb, buf);
    }
    lv_label_set_long_mode(lb, LV_LABEL_LONG_CLIP);
    if (font) {
        h = lv_font_get_line_height(font);
        if (h > 0) {
            lv_obj_set_height(lb, h);
        }
    }
}

static int slot_px(lv_obj_t *lb)
{
    int w;
    if (!lb) {
        return 0;
    }
    w = (int)lv_obj_get_style_width(lb, LV_PART_MAIN);
    if (w <= 0 || w > 4000) {
        lv_obj_update_layout(lb);
        w = (int)lv_obj_get_width(lb);
    }
    if (w <= 0 || w > 4000) {
        return 0;
    }
    return w;
}

static void set_lab_fit(lv_obj_t *lb, const char *text)
{
    int w = slot_px(lb);
    if (w <= 0) {
        set_lab(lb, text);
        if (lb) {
            lv_label_set_long_mode(lb, LV_LABEL_LONG_CLIP);
        }
        return;
    }
    set_lab_fit_px(lb, text, w);
}

static lv_obj_t *box_at(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    plain(o);
    return o;
}

static lv_obj_t *box_rect(lv_obj_t *parent, zk_rect r, uint32_t bg, int radius)
{
    return box_at(parent, r.x, r.y, r.w, r.h, bg, radius);
}

static void border_on(lv_obj_t *o, int width)
{
    lv_obj_set_style_border_width(o, width, 0);
    lv_obj_set_style_border_color(o, hex(COL_LINE), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
}

static void press_style(lv_obj_t *o, uint32_t bg)
{
    lv_obj_set_style_bg_color(o, lv_color_darken(hex(bg), LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(o, 4, LV_STATE_PRESSED);
}

static void on_evt(lv_event_t *e);

static lv_obj_t *target(lv_obj_t *parent, zk_rect r, int id, uint32_t bg, int radius, int border)
{
    lv_obj_t *o = box_rect(parent, r, bg, radius);
    void *ud = (void *)(intptr_t)id;
    if (border) {
        border_on(o, 3);
    }
    press_style(o, bg);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, on_evt, LV_EVENT_CLICKED, ud);
    lv_obj_add_event_cb(o, on_evt, LV_EVENT_PRESSED, ud);
    lv_obj_add_event_cb(o, on_evt, LV_EVENT_RELEASED, ud);
    lv_obj_add_event_cb(o, on_evt, LV_EVENT_PRESS_LOST, ud);
    if (id > 0 && id < HIT_N) {
        g_w.hit[id] = o;
    }
    return o;
}

static lv_obj_t *icon_at(lv_obj_t *parent, const void *src, uint32_t color, int x, int y)
{
    lv_obj_t *im = lv_image_create(parent);
    lv_image_set_src(im, src);
    lv_obj_set_pos(im, x, y);
    lv_obj_set_style_image_recolor(im, hex(color), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    plain(im);
    return im;
}

static lv_obj_t *lab(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *lb = lv_label_create(parent);
    lv_label_set_text(lb, text ? text : "");
    lv_obj_set_style_text_font(lb, font, 0);
    lv_obj_set_style_text_color(lb, hex(color), 0);
    lv_obj_set_pos(lb, x, y);
    lv_label_set_long_mode(lb, LV_LABEL_LONG_CLIP);
    plain(lb);
    remember(lb);
    return lb;
}

static void lab_width(lv_obj_t *lb, int w, lv_text_align_t align)
{
    lv_obj_set_width(lb, w);
    lv_obj_set_style_text_align(lb, align, 0);
}

static int fault_on(const zk_snapshot_t *s)
{
    if (!s->have_kiosk) {
        return 0;
    }
    if (s->kiosk.fault || s->kiosk.lockout) {
        return 1;
    }
    return s->kiosk.last_error[0] ? 1 : 0;
}

static int base_screen(const zk_snapshot_t *s)
{
    if (s->api_state == ZK_API_NEEDS_UPDATE) {
        return ZK_SCREEN_NEEDS_UPDATE;
    }
    if (s->have_kiosk && s->kiosk.has_run) {
        return ZK_SCREEN_RUNNING;
    }
    if (s->have_kiosk && s->kiosk.pause.paused) {
        return ZK_SCREEN_PAUSED;
    }
    return ZK_SCREEN_HOME;
}

static int station_still_there(const zk_snapshot_t *s)
{
    if (!s->have_kiosk || !g_station_id[0]) {
        return g_station_i >= 0 && s->have_kiosk && g_station_i < s->kiosk.n_stations;
    }
    return zk_kiosk_station_index(&s->kiosk, g_station_id) >= 0;
}

static void resolve(const zk_snapshot_t *s, int *content, int *modal)
{
    int base = base_screen(s);
    if (g_nav == ZK_SCREEN_STATION && !station_still_there(s)) {
        g_nav = -1;
        g_station_id[0] = 0;
        g_station_i = -1;
    }
    if (g_override == ZK_SCREEN_CONFIRM_STOP || g_override == ZK_SCREEN_CONFIRM_PAUSE) {
        *modal = g_override;
        *content = base;
        if (g_nav == ZK_SCREEN_PICKER || g_nav == ZK_SCREEN_SCHEDULES || g_nav == ZK_SCREEN_STATION) {
            if (base != ZK_SCREEN_NEEDS_UPDATE) {
                *content = g_nav;
            }
        }
        return;
    }
    if (g_override >= 0) {
        *modal = 0;
        *content = g_override;
        return;
    }
    *modal = g_modal;
    if (base == ZK_SCREEN_NEEDS_UPDATE) {
        *content = base;
        return;
    }
    if (g_nav == ZK_SCREEN_PICKER || g_nav == ZK_SCREEN_SCHEDULES || g_nav == ZK_SCREEN_STATION) {
        *content = g_nav;
    } else {
        *content = base;
    }
}

static void fill_key(const zk_snapshot_t *s, int content, int modal, view_key *k, zk_layout_in *in)
{
    int i;
    int nst;
    memset(k, 0, sizeof *k);
    memset(in, 0, sizeof *in);
    k->content = content;
    k->modal = modal;
    k->have = s->have_kiosk ? 1 : 0;
    k->fault = fault_on(s);
    k->rain = (s->have_kiosk && zk_rain_strip_visible(&s->kiosk)) ? 1 : 0;
    nst = (s->have_kiosk && s->kiosk.n_stations > 0) ? s->kiosk.n_stations : 0;
    if (nst > 8) {
        nst = 8;
    }
    k->n_st = nst;
    k->station_i = g_station_i;
    if (s->have_kiosk) {
        for (i = 0; i < nst; i++) {
            if (zk_station_soil_percent(&s->kiosk, i) >= 0) {
                k->soil_mask |= 1 << i;
            }
        }
    }
    if (s->have_schedules && s->schedules.n > 0) {
        if (g_sched_i < 0) {
            g_sched_i = 0;
        }
        if (g_sched_i >= s->schedules.n) {
            g_sched_i = 0;
        }
        k->sched_i = g_sched_i;
        if (content == ZK_SCREEN_SCHEDULES && g_sched_i < s->schedules.n) {
            int steps = s->schedules.items[g_sched_i].n_steps;
            if (steps < 0) {
                steps = 0;
            }
            if (steps > 5) {
                steps = 5;
            }
            k->steps = steps;
        }
    }
    in->n_stations = nst;
    in->n_schedules = k->steps;
    if (content == ZK_SCREEN_HOME) {
        in->rain_visible = (k->rain || k->fault) ? 1 : 0;
    } else if (content == ZK_SCREEN_PAUSED) {
        in->rain_visible = k->rain;
    }
}

static const char *station_title(const zk_snapshot_t *s, const char *id)
{
    const zk_kiosk_station_t *st;
    if (!id || !id[0]) {
        return "";
    }
    st = s->have_kiosk ? zk_kiosk_station(&s->kiosk, id) : NULL;
    if (st && st->title[0]) {
        return st->title;
    }
    return id;
}

static void name_upper(const zk_schedule_t *sch, char *out, size_t cap)
{
    char tmp[ZK_NOTE_MAX];
    zk_str_trim_copy(tmp, sizeof tmp, sch->note);
    if (!tmp[0]) {
        zk_str_trim_copy(tmp, sizeof tmp, sch->id);
    }
    zk_str_upper(tmp);
    zk_str_copy(out, cap, tmp);
}

static void sched_summary(const zk_schedule_t *sch, char *out, size_t cap)
{
    static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const int order[7] = {1, 2, 3, 4, 5, 6, 0};
    char t[16];
    char days[48];
    unsigned m;
    int n = 0;
    int i;
    size_t pos = 0;
    if (!sch || !sch->has_start) {
        zk_str_copy(out, cap, "--");
        return;
    }
    zk_fmt_time12(sch->start_hh, sch->start_mm, t, sizeof t);
    m = sch->weekdays;
    if (m == 0 || m == ZK_WD_ALL) {
        snprintf(out, cap, "Daily %s", t);
        return;
    }
    if (m == (ZK_WD_MON | ZK_WD_TUE | ZK_WD_WED | ZK_WD_THU | ZK_WD_FRI)) {
        snprintf(out, cap, "Weekdays %s", t);
        return;
    }
    days[0] = 0;
    for (i = 0; i < 7; i++) {
        int w = order[i];
        int wr;
        if ((m & (1u << w)) == 0) {
            continue;
        }
        wr = snprintf(days + pos, sizeof days - pos, n ? " %s" : "%s", wd[w]);
        if (wr < 0 || pos + (size_t)wr >= sizeof days) {
            break;
        }
        pos += (size_t)wr;
        n++;
    }
    snprintf(out, cap, "%s %s", days, t);
}

static void ends_about(const zk_schedule_t *sch, char *line, size_t cap)
{
    int mins = 0;
    int i;
    int total;
    char t[16];
    if (!sch || !sch->has_start) {
        zk_str_copy(line, cap, "--");
        return;
    }
    for (i = 0; i < sch->n_steps; i++) {
        if (sch->steps[i].minutes > 0) {
            mins += sch->steps[i].minutes;
        }
    }
    total = sch->start_hh * 60 + sch->start_mm + mins;
    total %= 24 * 60;
    if (total < 0) {
        total += 24 * 60;
    }
    zk_fmt_time12(total / 60, total % 60, t, sizeof t);
    snprintf(line, cap, "%s", t);
}

static void rain_parts(const zk_rain_strip_t *rs, char *bold, size_t cb, char *rest, size_t cr)
{
    char amt[16];
    int hours;
    hours = rs && (rs->hours == 24 || rs->hours == 72) ? rs->hours : (rs ? rs->hours : 24);
    if (hours <= 0) {
        hours = 24;
    }
    zk_fmt_inches(rs ? rs->inches : 0, amt, sizeof amt);
    snprintf(bold, cb, "Rain %s in", amt);
    snprintf(rest, cr, " in the last %d h", hours);
}

static void show_toast(const char *msg)
{
    snprintf(g_toast_text, sizeof g_toast_text, "%s", msg ? msg : "");
    g_toast_until = zk_platform_mono() + 2.5;
    if (g_w.toast_lbl) {
        set_lab_fit(g_w.toast_lbl, g_toast_text);
    }
    if (g_w.toast) {
        lv_obj_remove_flag(g_w.toast, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_w.toast);
    }
}

static void note_result(void)
{
    zk_action_result_t r;
    char buf[64];
    zk_data_action_result(&r);
    if (!r.done) {
        return;
    }
    if (g_seen_ok && r.seq == g_seen_seq) {
        return;
    }
    g_seen_ok = 1;
    g_seen_seq = r.seq;
    if (r.code == ZK_RO) {
        show_toast("Read-only mode: no request sent");
    } else if (r.code == ZK_OK) {
        show_toast("Sent");
    } else if (r.http_status > 0) {
        snprintf(buf, sizeof buf, "Failed (HTTP %d)", r.http_status);
        show_toast(buf);
    } else {
        show_toast("Could not reach the controller");
    }
}

static void submit_pause(const zk_snapshot_t *s)
{
    zk_pause_preview_t pv;
    int chip = g_pick_chip;
    if (chip < 0 || chip > 3) {
        chip = 0;
    }
    if (zk_pause_preview(s->have_schedules ? &s->schedules : NULL, s->wall,
                         (zk_pause_kind_t)chip, &pv) != ZK_OK) {
        zk_data_submit_action(ZK_ACT_PAUSE, "{\"reason\":\"kiosk\"}");
        return;
    }
    zk_data_submit_action(ZK_ACT_PAUSE, pv.body);
}

static void close_modal(void)
{
    g_modal = 0;
    if (g_override == ZK_SCREEN_CONFIRM_STOP || g_override == ZK_SCREEN_CONFIRM_PAUSE) {
        g_override = -1;
    }
    g_dirty = 1;
}

static void on_evt(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    zk_snapshot_t snap;

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (id == ZK_TARGET_STOP && g_w.shadow) {
            if (code == LV_EVENT_PRESSED) {
                lv_obj_add_flag(g_w.shadow, LV_OBJ_FLAG_HIDDEN);
            } else if (g_pressed != ZK_TARGET_STOP) {
                lv_obj_remove_flag(g_w.shadow, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (id == ZK_TARGET_HOLD_EDIT && g_w.hold_face) {
            if (code == LV_EVENT_PRESSED) {
                lv_obj_add_state(g_w.hold_face, LV_STATE_PRESSED);
            } else if (g_pressed != ZK_TARGET_HOLD_EDIT) {
                lv_obj_remove_state(g_w.hold_face, LV_STATE_PRESSED);
            }
        }
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    if (g_key.modal && id != ZK_TARGET_MODAL_OK && id != ZK_TARGET_MODAL_CANCEL) {
        return;
    }
    /* 700 ms after OK: a stray tap must not open or confirm another pause or STOP. */
    if (zk_platform_mono() < g_quiet_until &&
        ((id >= ZK_TARGET_CHIP0 && id <= ZK_TARGET_CHIP3) || id == ZK_TARGET_MODAL_OK ||
         id == ZK_TARGET_MODAL_CANCEL)) {
        return;
    }
    switch (id) {
    case ZK_TARGET_STOP:
        g_modal = ZK_SCREEN_CONFIRM_STOP;
        g_dirty = 1;
        break;
    case ZK_TARGET_PAUSE:
        g_nav = ZK_SCREEN_PICKER;
        g_modal = 0;
        g_dirty = 1;
        break;
    case ZK_TARGET_BACK:
        g_nav = -1;
        g_modal = 0;
        g_dirty = 1;
        break;
    case ZK_TARGET_RESUME:
        zk_data_submit_action(ZK_ACT_RESUME, NULL);
        note_result();
        break;
    case ZK_TARGET_HEADER_NEXT:
    case ZK_TARGET_INFO_CARD:
    case ZK_TARGET_SCHEDULES:
        g_nav = ZK_SCREEN_SCHEDULES;
        g_modal = 0;
        g_dirty = 1;
        break;
    case ZK_TARGET_CHIP0:
    case ZK_TARGET_CHIP1:
    case ZK_TARGET_CHIP2:
    case ZK_TARGET_CHIP3:
        g_pick_chip = id - ZK_TARGET_CHIP0;
        g_modal = ZK_SCREEN_CONFIRM_PAUSE;
        g_dirty = 1;
        break;
    case ZK_TARGET_HOLD_EDIT:
        show_toast("Editing arrives in a later update");
        break;
    case ACT_SCHED_BODY:
        zk_data_get(&snap);
        if (snap.have_schedules && snap.schedules.n > 1) {
            g_sched_i = (g_sched_i + 1) % snap.schedules.n;
            g_dirty = 1;
        }
        break;
    case ZK_TARGET_MODAL_CANCEL:
        close_modal();
        break;
    case ZK_TARGET_MODAL_OK:
        zk_data_get(&snap);
        if (g_key.modal == ZK_SCREEN_CONFIRM_PAUSE) {
            submit_pause(&snap);
            g_nav = -1; /* leave the picker; back to the base screen */
        } else {
            zk_data_submit_action(ZK_ACT_STOP, NULL);
        }
        g_quiet_until = zk_platform_mono() + 0.700;
        close_modal();
        note_result();
        break;
    default:
        if (id >= ZK_TARGET_TILE0 && id <= ZK_TARGET_TILE7) {
            int ti = id - ZK_TARGET_TILE0;
            zk_data_get(&snap);
            g_station_i = ti;
            g_station_id[0] = 0;
            if (snap.have_kiosk && ti >= 0 && ti < snap.kiosk.n_stations) {
                zk_str_copy(g_station_id, sizeof g_station_id, snap.kiosk.stations[ti].id);
            }
            g_nav = ZK_SCREEN_STATION;
            g_modal = 0;
            g_dirty = 1;
        }
        break;
    }
}

static void build_stop(lv_obj_t *scr, zk_rect r, int running)
{
    zk_rect sh = r;
    const lv_font_t *word_font = running ? &zk_font_xb_104 : &zk_font_xb_96;
    int word_h = running ? 75 : 70;
    int block;
    int y0;
    lv_obj_t *word;
    lv_obj_t *sub;
    sh.y += 6;
    g_w.shadow = box_rect(scr, sh, COL_STOPSH, 30);
    g_w.hit[ZK_TARGET_STOP] = target(scr, r, ZK_TARGET_STOP, COL_STOP, 30, 0);
    block = 80 + word_h + 8 + 28;
    y0 = (r.h - block) / 2;
    if (y0 < 6) {
        y0 = 6;
    }
    icon_at(g_w.hit[ZK_TARGET_STOP], &zk_icon_stop_oct_84, COL_WHITE, (r.w - 84) / 2, y0);
    word = lab(g_w.hit[ZK_TARGET_STOP], "STOP", word_font, COL_WHITE, 0, y0 + 78);
    lab_width(word, r.w, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(word, running ? 2 : 4, 0);
    sub = lab(g_w.hit[ZK_TARGET_STOP], running ? "water off now" : "all water off",
              &zk_font_sb_26, COL_WHITE, 0, y0 + 78 + word_h + 2);
    lab_width(sub, r.w, LV_TEXT_ALIGN_CENTER);
}

static void build_slot(lv_obj_t *scr, zk_rect r, int id, const char *text, const void *ic, uint32_t bg, uint32_t fg, int horizontal)
{
    lv_obj_t *b = target(scr, r, id, bg, 26, 0);
    lv_obj_t *lb;
    if (horizontal) {
        int y = (r.h - 44) / 2;
        icon_at(b, ic, fg, 36, y);
        lb = lab(b, text, &zk_font_xb_42, fg, 36 + 44 + 12, y + 2);
        (void)lb;
    } else {
        int block = 44 + 10 + 43;
        int y0 = (r.h - block) / 2;
        icon_at(b, ic, fg, (r.w - 44) / 2, y0);
        lb = lab(b, text, &zk_font_xb_42, fg, 0, y0 + 44 + 8);
        lab_width(lb, r.w, LV_TEXT_ALIGN_CENTER);
    }
}

static void build_rail(lv_obj_t *scr, const zk_layout_rects *L, int content)
{
    int running = content == ZK_SCREEN_RUNNING;
    build_stop(scr, L->stop, running);
    if (content == ZK_SCREEN_NEEDS_UPDATE) {
        return;
    }
    if (content == ZK_SCREEN_HOME || content == ZK_SCREEN_RUNNING) {
        build_slot(scr, L->slot, ZK_TARGET_PAUSE, "Pause", &zk_icon_pause_44, COL_PAUSE, COL_WHITE, running);
    } else if (content == ZK_SCREEN_PAUSED) {
        build_slot(scr, L->slot, ZK_TARGET_RESUME, "Resume", &zk_icon_play_44, COL_TEAL, COL_WHITE, 0);
    } else if (content == ZK_SCREEN_STATION) {
        build_slot(scr, L->slot, ZK_TARGET_BACK, "Close", &zk_icon_back_44, COL_BACK, COL_INK, 0);
    } else {
        build_slot(scr, L->slot, ZK_TARGET_BACK, "Back", &zk_icon_back_44, COL_BACK, COL_INK, 0);
    }
}

static void build_schedules_btn(lv_obj_t *scr, zk_rect r)
{
    lv_obj_t *b;
    lv_obj_t *lb;
    int y0;
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    b = target(scr, r, ZK_TARGET_SCHEDULES, COL_CARD, 22, 1);
    y0 = (r.h - 44 - 8 - 28) / 2;
    if (y0 < 6) {
        y0 = 6;
    }
    icon_at(b, &zk_icon_list_52, COL_TEAL, (r.w - 52) / 2, y0);
    mark_lab("sched_btn", 1);
    lb = lab(b, "Schedules", &zk_font_b_26, COL_INK, 4, y0 + 44 + 4);
    lab_width(lb, r.w - 8, LV_TEXT_ALIGN_CENTER);
}

static uint32_t station_color(const char *c)
{
    unsigned r, g, b;
    if (!c || !c[0]) {
        return COL_TEAL;
    }
    if (c[0] == '#' && strlen(c) >= 7 &&
        sscanf(c, "#%2x%2x%2x", &r, &g, &b) == 3) {
        return (r << 16) | (g << 8) | b;
    }
    if (strcmp(c, "red") == 0) {
        return 0xC44536;
    }
    if (strcmp(c, "yellow") == 0) {
        return 0xD4A017;
    }
    if (strcmp(c, "blue") == 0) {
        return 0x2A6F97;
    }
    if (strcmp(c, "green") == 0) {
        return 0x4F7C4A;
    }
    return COL_TEAL;
}

static void build_home(lv_obj_t *scr, const zk_layout_rects *L, const zk_snapshot_t *s, const view_key *k)
{
    lv_obj_t *hdr;
    int i;
    hdr = target(scr, L->header, ZK_TARGET_HEADER_NEXT, COL_BG, 0, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(hdr, 0, LV_STATE_PRESSED);
    mark_lab("hdr_kicker", 1);
    g_w.hdr_kicker = lab(hdr, "", &zk_font_b_26, COL_MUT, 4, 6);
    lab_width(g_w.hdr_kicker, L->header.w - 8, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(g_w.hdr_kicker, 1, 0);
    mark_lab("hdr_big", 1);
    g_w.hdr_big = lab(hdr, "", &zk_font_xb_50, COL_INK, 4, 38);
    lab_width(g_w.hdr_big, L->header.w - 8, LV_TEXT_ALIGN_LEFT);
    build_schedules_btn(scr, L->schedules);
    if (k->fault && L->rain.w > 0) {
        lv_obj_t *pill = box_rect(scr, L->rain, COL_STOP, 16);
        mark_lab("fault_lbl", 1);
        g_w.fault_lbl = lab(pill, "", &zk_font_b_26, COL_WHITE, 16, 14);
        lab_width(g_w.fault_lbl, L->rain.w - 32, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(g_w.fault_lbl, LV_LABEL_LONG_CLIP);
    } else if (k->rain && L->rain.w > 0) {
        lv_obj_t *strip = box_rect(scr, L->rain, COL_RAINBG, 16);
        icon_at(strip, &zk_icon_cloud_36, COL_RAINFG, 14, 11);
        mark_lab("rain_bold", 1);
        g_w.rain_bold = lab(strip, "", &zk_font_xb_28, COL_RAINFG, 62, 14);
        mark_lab("rain_rest", 1);
        g_w.rain_rest = lab(strip, "", &zk_font_sb_28, COL_RAINFG, 200, 14);
    }
    g_w.n_tiles = L->n_tiles;
    for (i = 0; i < L->n_tiles && i < 8; i++) {
        zk_rect tr = L->tiles[i];
        int soil = (k->soil_mask & (1 << i)) != 0;
        lv_obj_t *tile = target(scr, tr, ZK_TARGET_TILE0 + i, COL_CARD, 22, 1);
        int title_y = soil ? 10 : (tr.h - 36) / 2;
        mark_lab_n("tile_title", i, 1);
        g_w.tile_title[i] = lab(tile, "", &zk_font_xb_32, COL_INK, 14, title_y);
        lab_width(g_w.tile_title[i], tr.w - 28, LV_TEXT_ALIGN_LEFT);
        if (!soil) {
            continue;
        }
        {
            int pct_w = 92;
            int bar_h = 38;
            int bar_x = 14;
            int bar_w = tr.w - 14 - 10 - pct_w - 12;
            int bar_y = tr.h - 14 - bar_h;
            lv_obj_t *bar;
            int seg;
            if (bar_w < 40) {
                bar_w = 40;
            }
            if (bar_y < title_y + 36) {
                bar_y = title_y + 36;
            }
            bar = box_at(tile, bar_x, bar_y, bar_w, bar_h, COL_TRACK, 12);
            border_on(bar, 2);
            lv_obj_set_style_clip_corner(bar, true, 0);
            g_w.tile_bar_w[i] = bar_w - 4;
            g_w.tile_fill[i] = box_at(bar, 2, 2, 0, bar_h - 4, COL_FILL, 8);
            for (seg = 1; seg <= 9; seg++) {
                int x = 2 + seg * g_w.tile_bar_w[i] / 10 - 1;
                box_at(bar, x, 2, 3, bar_h - 4, COL_CARD, 0);
            }
            g_w.tile_pct[i] = lab(tile, "", &zk_font_xb_44, COL_INK, tr.w - 12 - pct_w, bar_y + (bar_h - 45) / 2);
            lab_width(g_w.tile_pct[i], pct_w, LV_TEXT_ALIGN_RIGHT);
            g_w.tile_pill[i] = box_at(tile, tr.w - 14 - 132, 8, 132, 32, COL_TEAL, 16);
            {
                lv_obj_t *pl = lab(g_w.tile_pill[i], "WATERING", &zk_font_b_26, COL_WHITE, 0, 2);
                lab_width(pl, 132, LV_TEXT_ALIGN_CENTER);
            }
            lv_obj_add_flag(g_w.tile_pill[i], LV_OBJ_FLAG_HIDDEN);
            (void)s;
        }
    }
}

static void build_running(lv_obj_t *scr, const zk_layout_rects *L)
{
    zk_rect card = L->main;
    lv_obj_t *c = box_rect(scr, card, COL_RUNBG, 26);
    int bar_w = card.w - 48;
    int prog_y = card.h - 18 - 32 - 16 - 24;
    mark_lab("run_title", 1);
    g_w.run_title = lab(c, "", &zk_font_xb_58, COL_WHITE, 24, 58);
    lab_width(g_w.run_title, card.w - 48, LV_TEXT_ALIGN_LEFT);
    icon_at(c, &zk_icon_drop_30, COL_RUNLBL, 24, 16);
    {
        lv_obj_t *lb = lab(c, "WATERING NOW", &zk_font_b_26, COL_RUNLBL, 24 + 30 + 10, 18);
        lv_obj_set_style_text_letter_space(lb, 1, 0);
    }
    g_w.run_count = lab(c, "", &zk_font_xb_172, COL_WHITE, 20, 128);
    lv_obj_set_style_text_letter_space(g_w.run_count, -2, 0);
    g_w.run_bar_w = bar_w;
    {
        lv_obj_t *track = box_at(c, 24, prog_y, bar_w, 24, COL_WHITE, 12);
        lv_obj_set_style_bg_opa(track, 56, 0);
        lv_obj_set_style_clip_corner(track, true, 0);
        g_w.run_fill = box_at(track, 0, 0, 0, 24, COL_RUNFILL, 12);
    }
    mark_lab("run_step", 0);
    g_w.run_step = lab(c, "", &zk_font_b_28, COL_WHITE, 24, card.h - 18 - 30);
    mark_lab("run_next", 1);
    g_w.run_next = lab(c, "", &zk_font_b_28, COL_WHITE, card.w / 2, card.h - 18 - 30);
    lab_width(g_w.run_next, card.w / 2 - 24, LV_TEXT_ALIGN_RIGHT);
}

static void build_paused(lv_obj_t *scr, const zk_layout_rects *L, const view_key *k)
{
    lv_obj_t *ban;
    lv_obj_t *info;
    if (L->banner.w <= 0) {
        return;
    }
    build_schedules_btn(scr, L->schedules);
    ban = box_rect(scr, L->banner, COL_PAUSE, 24);
    icon_at(ban, &zk_icon_cloud_36, COL_WHITE, 14, (L->banner.h - 36) / 2);
    mark_lab("ban_title", 1);
    g_w.ban_title = lab(ban, "", &zk_font_b_26, COL_WHITE, 14 + 36 + 12, 18);
    lv_obj_set_style_text_letter_space(g_w.ban_title, 1, 0);
    lab_width(g_w.ban_title, L->banner.w - 14 - 36 - 12 - 12, LV_TEXT_ALIGN_LEFT);
    mark_lab("ban_until", 1);
    g_w.ban_until = lab(ban, "", &zk_font_xb_36, COL_WHITE, 14 + 36 + 12, 54);
    lab_width(g_w.ban_until, L->banner.w - 14 - 36 - 12 - 12, LV_TEXT_ALIGN_LEFT);
    if (k->rain && L->rain.w > 0) {
        lv_obj_t *strip = box_rect(scr, L->rain, COL_RAINBG, 16);
        icon_at(strip, &zk_icon_cloud_36, COL_RAINFG, 14, 11);
        mark_lab("rain_bold", 1);
        g_w.rain_bold = lab(strip, "", &zk_font_xb_28, COL_RAINFG, 62, 14);
        mark_lab("rain_rest", 1);
        g_w.rain_rest = lab(strip, "", &zk_font_sb_28, COL_RAINFG, 200, 14);
    }
    if (L->info.w <= 0) {
        return;
    }
    info = target(scr, L->info, ZK_TARGET_INFO_CARD, COL_CARD, 26, 1);
    {
        lv_obj_t *kicker = lab(info, "ON HOLD", &zk_font_b_26, COL_MUT, 20, 16);
        lv_obj_set_style_text_letter_space(kicker, 1, 0);
    }
    mark_lab("info_name", 1);
    g_w.info_name = lab(info, "", &zk_font_xb_32, COL_INK, 20, 54);
    lab_width(g_w.info_name, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
    mark_lab("info_sub0", 0);
    g_w.info_sub0 = lab(info, "Runs again after you resume", &zk_font_sb_28, COL_MUT, 20, 112);
    lab_width(g_w.info_sub0, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
    mark_lab("info_sub1", 0);
    g_w.info_sub1 = lab(info, "or when the pause ends.", &zk_font_sb_28, COL_MUT, 20, 146);
    lab_width(g_w.info_sub1, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
}

static const void *chip_icon(int i)
{
    switch (i) {
    case 0:
        return &zk_icon_sunrise_52;
    case 1:
        return &zk_icon_cloud_52;
    case 2:
        return &zk_icon_list_52;
    default:
        return &zk_icon_inf_52;
    }
}

static void build_picker(lv_obj_t *scr, const zk_layout_rects *L)
{
    int i;
    lv_obj_t *title = lab(scr, "Pause watering", &zk_font_xb_44, COL_INK, L->title.x + 2, L->title.y + 2);
    (void)title;
    for (i = 0; i < 4; i++) {
        lv_obj_t *c = target(scr, L->chips[i], ZK_TARGET_CHIP0 + i, COL_CARD, 24, 1);
        icon_at(c, chip_icon(i), COL_TEAL, 16, 12);
        mark_lab_n("chip_t", i, 1);
        g_w.chip_t[i] = lab(c, "", &zk_font_xb_36, COL_INK, 16, 72);
        lab_width(g_w.chip_t[i], L->chips[i].w - 32, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(g_w.chip_t[i], LV_LABEL_LONG_WRAP);
        /* Two lines is the current picker copy. A longer primary clips here
         * instead of pushing the subtitle. One line keeps its natural height. */
        lv_obj_set_style_max_height(g_w.chip_t[i], 2 * lv_font_get_line_height(&zk_font_xb_36), 0);
        mark_lab_n("chip_s", i, 1);
        g_w.chip_s[i] = lab(c, "", &zk_font_sb_26, COL_MUT, 16, 120);
        lab_width(g_w.chip_s[i], L->chips[i].w - 32, LV_TEXT_ALIGN_LEFT);
    }
}

static void dashes(lv_obj_t *parent, int y, int x0, int x1)
{
    int x;
    for (x = x0; x < x1; x += 16) {
        int w = 10;
        if (x + w > x1) {
            w = x1 - x;
        }
        if (w > 0) {
            box_at(parent, x, y, w, 3, COL_LINE, 0);
        }
    }
}

static void build_schedules(lv_obj_t *scr, const zk_layout_rects *L, const zk_snapshot_t *s, const view_key *k)
{
    lv_obj_t *card;
    lv_obj_t *hold;
    lv_obj_t *inner;
    int i;
    int nrows;
    lv_obj_t *title = lab(scr, "Schedules", &zk_font_xb_44, COL_INK, L->title.x + 2, L->title.y + 2);
    (void)title;
    mark_lab("sched_count", 1);
    g_w.sched_count = lab(scr, "", &zk_font_sb_26, COL_MUT, L->title.x, L->title.y + 12);
    lab_width(g_w.sched_count, L->title.w - 4, LV_TEXT_ALIGN_RIGHT);
    card = box_rect(scr, L->card, COL_CARD, 26);
    border_on(card, 3);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_evt, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_SCHED_BODY);
    mark_lab("sched_name", 1);
    g_w.sched_name = lab(card, "", &zk_font_b_26, COL_MUT, 20, 8);
    lv_obj_set_style_text_letter_space(g_w.sched_name, 1, 0);
    lab_width(g_w.sched_name, L->card.w - 40 - 170, LV_TEXT_ALIGN_LEFT);
    mark_lab("sched_sum", 1);
    g_w.sched_sum = lab(card, "", &zk_font_xb_50, COL_INK, 20, 32);
    lab_width(g_w.sched_sum, L->card.w - 40 - 170, LV_TEXT_ALIGN_LEFT);
    g_w.sched_ends1 = lab(card, "Ends about", &zk_font_b_28, COL_MUT, L->card.w - 20 - 168, 8);
    lab_width(g_w.sched_ends1, 168, LV_TEXT_ALIGN_RIGHT);
    mark_lab("sched_ends2", 1);
    g_w.sched_ends2 = lab(card, "", &zk_font_b_28, COL_MUT, L->card.w - 20 - 168, 38);
    lab_width(g_w.sched_ends2, 168, LV_TEXT_ALIGN_RIGHT);
    nrows = L->n_sched_rows;
    if (nrows > k->steps) {
        nrows = k->steps;
    }
    if (nrows > 5) {
        nrows = 5;
    }
    g_w.n_rows = nrows;
    for (i = 0; i < nrows; i++) {
        int y = L->sched_rows[i].y - L->card.y;
        int h = L->sched_rows[i].h;
        mark_lab_n("row_name", i, 1);
        g_w.row_name[i] = lab(card, "", &zk_font_b_34, COL_INK, 20, y + 6);
        /* Right edge meets row_min (x = card.w-170). Short names stay left-aligned. */
        lab_width(g_w.row_name[i], L->card.w - 190, LV_TEXT_ALIGN_LEFT);
        g_w.row_min[i] = lab(card, "", &zk_font_xb_42, COL_INK, L->card.w - 20 - 150, y + 2);
        lab_width(g_w.row_min[i], 80, LV_TEXT_ALIGN_RIGHT);
        g_w.row_unit[i] = lab(card, "min", &zk_font_b_26, COL_INK, L->card.w - 20 - 64, y + 14);
        if (i + 1 < nrows) {
            dashes(card, y + h - 3, 20, L->card.w - 20);
        }
    }
    hold = target(scr, L->hold_edit, ZK_TARGET_HOLD_EDIT, COL_CARD, 0, 0);
    lv_obj_set_style_bg_opa(hold, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hold, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(hold, 0, LV_STATE_PRESSED);
    inner = box_at(hold, 20, 0, L->hold_edit.w - 40, L->hold_edit.h - 10, COL_PAUSE, 24);
    g_w.hold_face = inner;
    lv_obj_set_style_bg_color(inner, lv_color_darken(hex(COL_PAUSE), LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(inner, 4, LV_STATE_PRESSED);
    {
        lv_point_t sz;
        int group;
        int x0;
        int ih = L->hold_edit.h - 10;
        int ring = 52;
        lv_text_get_size(&sz, "Hold to edit", &zk_font_xb_40, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        group = ring + 16 + sz.x + 16 + 40;
        x0 = (L->hold_edit.w - 40 - group) / 2;
        if (x0 < 8) {
            x0 = 8;
        }
        {
            lv_obj_t *arc = lv_arc_create(inner);
            lv_obj_set_size(arc, ring, ring);
            lv_obj_set_pos(arc, x0, (ih - ring) / 2);
            lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(arc, 0, 0);
            lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
            lv_arc_set_rotation(arc, 270);
            lv_arc_set_bg_angles(arc, 0, 360);
            lv_arc_set_value(arc, 25);
            lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(arc, hex(COL_WHITE), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, hex(COL_WHITE), LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
            lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
            plain(arc);
        }
        {
            lv_obj_t *lb = lab(inner, "Hold to edit", &zk_font_xb_40, COL_WHITE, x0 + ring + 16, (ih - 41) / 2);
            (void)lb;
        }
        icon_at(inner, &zk_icon_pencil_40, COL_WHITE, x0 + ring + 16 + sz.x + 16, (ih - 40) / 2);
    }
    (void)s;
}

static const char *station_state_label(const char *state)
{
    if (state && strcmp(state, "running") == 0) {
        return "Running";
    }
    if (state && strcmp(state, "queued") == 0) {
        return "Queued";
    }
    return "Idle";
}

static void build_station(lv_obj_t *scr, const zk_layout_rects *L)
{
    lv_obj_t *card;
    lv_obj_t *bar;
    int bar_w;
    if (L->title.w > 0) {
        lv_obj_t *t = lab(scr, "Station", &zk_font_xb_44, COL_INK, L->title.x + 2, L->title.y + 2);
        (void)t;
    }
    card = box_rect(scr, L->card, COL_CARD, 26);
    border_on(card, 3);
    mark_lab("st_title", 1);
    g_w.st_title = lab(card, "", &zk_font_xb_50, COL_INK, 20, 18);
    lab_width(g_w.st_title, L->card.w - 40, LV_TEXT_ALIGN_LEFT);
    g_w.st_dot = box_at(card, 20, 78, 22, 22, COL_TEAL, 11);
    mark_lab("st_state", 1);
    g_w.st_state = lab(card, "", &zk_font_xb_36, COL_INK, 52, 72);
    lab_width(g_w.st_state, L->card.w - 72, LV_TEXT_ALIGN_LEFT);
    mark_lab("st_exempt", 1);
    g_w.st_exempt = lab(card, "", &zk_font_sb_28, COL_MUT, 20, 118);
    lab_width(g_w.st_exempt, L->card.w - 40, LV_TEXT_ALIGN_LEFT);
    mark_lab("st_soil_msg", 1);
    g_w.st_soil_msg = lab(card, "", &zk_font_sb_28, COL_MUT, 20, 160);
    lab_width(g_w.st_soil_msg, L->card.w - 40, LV_TEXT_ALIGN_LEFT);
    bar_w = L->card.w - 40 - 92;
    if (bar_w < 40) {
        bar_w = 40;
    }
    bar = box_at(card, 20, 210, bar_w, 38, COL_TRACK, 12);
    border_on(bar, 2);
    lv_obj_set_style_clip_corner(bar, true, 0);
    g_w.st_bar_w = bar_w - 4;
    g_w.st_soil_fill = box_at(bar, 2, 2, 0, 34, COL_FILL, 8);
    mark_lab("st_soil_pct", 1);
    g_w.st_soil_pct = lab(card, "", &zk_font_xb_44, COL_INK, L->card.w - 20 - 92, 206);
    lab_width(g_w.st_soil_pct, 92, LV_TEXT_ALIGN_RIGHT);
}

static void build_needs_update(lv_obj_t *scr, const zk_layout_rects *L)
{
    lv_obj_t *card = box_rect(scr, L->card, COL_CARD, 26);
    border_on(card, 3);
    mark_lab("nu_title", 0);
    g_w.nu_title = lab(card, "Controller needs an update", &zk_font_xb_44, COL_INK, 24, 40);
    lab_width(g_w.nu_title, L->card.w - 48, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(g_w.nu_title, LV_LABEL_LONG_WRAP);
    mark_lab("nu_body", 1);
    g_w.nu_body = lab(card, "Please update the controller.", &zk_font_sb_28, COL_MUT, 24, 160);
    lab_width(g_w.nu_body, L->card.w - 48, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(g_w.nu_body, LV_LABEL_LONG_WRAP);
}

static void build_modal(lv_obj_t *scr, int modal)
{
    zk_layout_in in;
    zk_layout_rects M;
    lv_obj_t *dim;
    lv_obj_t *card;
    lv_obj_t *ok;
    lv_obj_t *cancel;
    uint32_t ok_bg = (modal == ZK_SCREEN_CONFIRM_PAUSE) ? COL_PAUSE : COL_STOP;
    const char *ok_txt = (modal == ZK_SCREEN_CONFIRM_PAUSE) ? "Pause" : "STOP";
    memset(&in, 0, sizeof in);
    zk_layout_rects_of((enum zk_screen)modal, &in, &M);
    dim = box_at(scr, 0, 0, ZK_SCREEN_W, ZK_SCREEN_H, COL_DIM, 0);
    lv_obj_set_style_bg_opa(dim, 150, 0);
    lv_obj_add_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    card = box_at(dim, M.modal.x, M.modal.y, M.modal.w, M.modal.h, COL_CARD, 26);
    border_on(card, 3);
    g_w.m_title = lab(card, "", &zk_font_xb_44, COL_INK, 24, 22);
    lab_width(g_w.m_title, M.modal.w - 48, LV_TEXT_ALIGN_CENTER);
    mark_lab("m_body", 1);
    g_w.m_body = lab(card, "", &zk_font_sb_28, COL_INK, 24, 84);
    lab_width(g_w.m_body, M.modal.w - 48, LV_TEXT_ALIGN_CENTER);
    mark_lab("m_sub", 1);
    g_w.m_sub = lab(card, "", &zk_font_sb_26, COL_MUT, 24, 122);
    lab_width(g_w.m_sub, M.modal.w - 48, LV_TEXT_ALIGN_CENTER);
    ok = target(dim, M.modal_ok, ZK_TARGET_MODAL_OK, ok_bg, 22, 0);
    cancel = target(dim, M.modal_cancel, ZK_TARGET_MODAL_CANCEL, COL_BACK, 22, 0);
    {
        lv_obj_t *lb = lab(ok, ok_txt, &zk_font_xb_42, COL_WHITE, 0, (M.modal_ok.h - 43) / 2);
        lab_width(lb, M.modal_ok.w, LV_TEXT_ALIGN_CENTER);
        lb = lab(cancel, "Cancel", &zk_font_xb_42, COL_INK, 0, (M.modal_cancel.h - 43) / 2);
        lab_width(lb, M.modal_cancel.w, LV_TEXT_ALIGN_CENTER);
    }
}

static void build_overlays(lv_obj_t *scr)
{
    g_w.stale = box_at(scr, 28, 416, 490, 48, COL_STALE_BG, 24);
    border_on(g_w.stale, 2);
    lv_obj_set_style_border_color(g_w.stale, hex(COL_STOP), 0);
    {
        lv_obj_t *lb = lab(g_w.stale, "Can't reach the controller", &zk_font_b_26, COL_STOP, 12, 10);
        lab_width(lb, 466, LV_TEXT_ALIGN_CENTER);
    }
    lv_obj_add_flag(g_w.stale, LV_OBJ_FLAG_HIDDEN);
    g_w.toast = box_at(scr, 28, 360, 490, 48, COL_INK, 24);
    mark_lab("toast", 1);
    g_w.toast_lbl = lab(g_w.toast, "", &zk_font_b_26, COL_CARD, 12, 10);
    lab_width(g_w.toast_lbl, 466, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(g_w.toast, LV_OBJ_FLAG_HIDDEN);
}

static void apply_rain(const char *bold, const char *rest)
{
    lv_obj_t *parent;
    lv_point_t sz;
    const char *shown;
    int parent_w;
    int bold_max;
    int x;
    int rest_max;
    if (!g_w.rain_bold || !g_w.rain_rest) {
        return;
    }
    parent = lv_obj_get_parent(g_w.rain_bold);
    parent_w = parent ? (int)lv_obj_get_style_width(parent, LV_PART_MAIN) : 0;
    if (parent_w <= 0 || parent_w > 4000) {
        if (parent) {
            lv_obj_update_layout(parent);
            parent_w = (int)lv_obj_get_width(parent);
        }
    }
    bold_max = parent_w - 62 - 14;
    if (bold_max < 8) {
        set_lab(g_w.rain_bold, bold);
        set_lab(g_w.rain_rest, rest);
        shown = lv_label_get_text(g_w.rain_bold);
        lv_text_get_size(&sz, shown ? shown : "", &zk_font_xb_28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_pos(g_w.rain_rest, 62 + sz.x, 14);
        return;
    }
    set_lab_fit_px(g_w.rain_bold, bold, bold_max);
    shown = lv_label_get_text(g_w.rain_bold);
    lv_text_get_size(&sz, shown ? shown : "", &zk_font_xb_28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    x = 62 + sz.x;
    lv_obj_set_pos(g_w.rain_rest, x, 14);
    rest_max = parent_w - x - 14;
    if (rest_max < 1) {
        rest_max = 1;
    }
    lv_obj_set_width(g_w.rain_rest, rest_max);
    set_lab_fit_px(g_w.rain_rest, rest, rest_max);
}

static void apply_station_sheet(const zk_snapshot_t *s)
{
    const zk_kiosk_station_t *st = NULL;
    int idx = -1;
    int pct;
    char a[160];
    if (s->have_kiosk) {
        if (g_station_id[0]) {
            idx = zk_kiosk_station_index(&s->kiosk, g_station_id);
        }
        if (idx < 0) {
            idx = g_station_i;
        }
        if (idx >= 0 && idx < s->kiosk.n_stations) {
            st = &s->kiosk.stations[idx];
            zk_str_copy(g_station_id, sizeof g_station_id, st->id);
            g_station_i = idx;
        }
    }
    if (!st) {
        set_lab_fit(g_w.st_title, "--");
        set_lab_fit(g_w.st_state, "");
        set_lab_fit(g_w.st_exempt, "");
        set_lab_fit(g_w.st_soil_msg, "");
        set_lab(g_w.st_soil_pct, "");
        return;
    }
    set_lab_fit(g_w.st_title, st->title[0] ? st->title : st->id);
    if (g_w.st_dot) {
        lv_obj_set_style_bg_color(g_w.st_dot, hex(station_color(st->color)), 0);
    }
    set_lab_fit(g_w.st_state, station_state_label(st->state));
    if (st->rain_pause_exempt) {
        set_lab_fit(g_w.st_exempt, "Skips rain pauses");
        lv_obj_remove_flag(g_w.st_exempt, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_lab_fit(g_w.st_exempt, "");
        lv_obj_add_flag(g_w.st_exempt, LV_OBJ_FLAG_HIDDEN);
    }
    pct = zk_station_soil_percent(&s->kiosk, idx);
    if (pct >= 0) {
        int low = zk_soil_low(pct);
        int fill_w;
        set_lab_fit(g_w.st_soil_msg, "");
        lv_obj_add_flag(g_w.st_soil_msg, LV_OBJ_FLAG_HIDDEN);
        snprintf(a, sizeof a, "%d%%", pct);
        set_lab(g_w.st_soil_pct, a);
        lv_obj_set_style_text_color(g_w.st_soil_pct, hex(low ? COL_LOWTX : COL_INK), 0);
        fill_w = g_w.st_bar_w * pct / 100;
        if (fill_w < 0) {
            fill_w = 0;
        }
        if (g_w.st_soil_fill) {
            lv_obj_remove_flag(lv_obj_get_parent(g_w.st_soil_fill), LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(g_w.st_soil_fill, fill_w);
            lv_obj_set_style_bg_color(g_w.st_soil_fill, hex(low ? COL_LOW : COL_FILL), 0);
        }
        if (g_w.st_soil_pct) {
            lv_obj_remove_flag(g_w.st_soil_pct, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (g_w.st_soil_fill) {
            lv_obj_add_flag(lv_obj_get_parent(g_w.st_soil_fill), LV_OBJ_FLAG_HIDDEN);
        }
        if (g_w.st_soil_pct) {
            lv_obj_add_flag(g_w.st_soil_pct, LV_OBJ_FLAG_HIDDEN);
        }
        if (s->kiosk.soil.enabled && s->kiosk.soil.et_stale) {
            set_lab_fit(g_w.st_soil_msg, "Soil estimate paused (ET data old)");
            lv_obj_remove_flag(g_w.st_soil_msg, LV_OBJ_FLAG_HIDDEN);
        } else {
            set_lab_fit(g_w.st_soil_msg, "");
            lv_obj_add_flag(g_w.st_soil_msg, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void apply_content(const zk_snapshot_t *s)
{
    char a[320];
    char b[192];
    char c[96];
    int i;

    if (g_key.content == ZK_SCREEN_HOME) {
        if (!s->have_kiosk) {
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " --");
            set_lab_fit(g_w.hdr_kicker, a);
            set_lab_fit(g_w.hdr_big, "Connecting" ELLIPSIS);
        } else if (!s->kiosk.has_next_run) {
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " --");
            set_lab_fit(g_w.hdr_kicker, a);
            set_lab_fit(g_w.hdr_big, "--");
        } else {
            zk_fmt_name_upper(s->kiosk.next_run.name, c, sizeof c);
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " %s", c[0] ? c : "--");
            set_lab_fit(g_w.hdr_kicker, a);
            zk_fmt_fire_when(&s->kiosk.next_run, s->wall, b, sizeof b);
            set_lab_fit(g_w.hdr_big, b[0] ? b : "--");
        }
        if (g_w.fault_lbl) {
            const char *err = (s->have_kiosk && s->kiosk.last_error[0]) ? s->kiosk.last_error : "check controller";
            snprintf(a, sizeof a, "Fault: %s", err);
            set_lab_fit(g_w.fault_lbl, a);
        }
        if (g_w.rain_bold && s->have_kiosk) {
            rain_parts(&s->kiosk.rain_strip, a, sizeof a, b, sizeof b);
            apply_rain(a, b);
        }
        for (i = 0; i < g_w.n_tiles; i++) {
            const zk_kiosk_station_t *stn;
            int pct = -1;
            int low;
            int fill_w;
            if (!s->have_kiosk || i >= s->kiosk.n_stations) {
                continue;
            }
            stn = &s->kiosk.stations[i];
            set_lab_fit(g_w.tile_title[i], stn->title);
            pct = zk_station_soil_percent(&s->kiosk, i);
            if (g_w.tile_pct[i] && pct >= 0) {
                low = zk_soil_low(pct);
                snprintf(a, sizeof a, "%d%%", pct);
                set_lab(g_w.tile_pct[i], a);
                lv_obj_set_style_text_color(g_w.tile_pct[i], hex(low ? COL_LOWTX : COL_INK), 0);
                fill_w = g_w.tile_bar_w[i] * pct / 100;
                if (fill_w < 0) {
                    fill_w = 0;
                }
                if (g_w.tile_fill[i]) {
                    lv_obj_set_width(g_w.tile_fill[i], fill_w);
                    lv_obj_set_style_bg_color(g_w.tile_fill[i], hex(low ? COL_LOW : COL_FILL), 0);
                }
            }
            if (g_w.tile_pill[i]) {
                int on = stn->on || strcmp(stn->state, "running") == 0;
                if (on) {
                    lv_obj_remove_flag(g_w.tile_pill[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(g_w.tile_pill[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (g_key.content == ZK_SCREEN_RUNNING) {
        const zk_kiosk_run_t *run = s->have_kiosk && s->kiosk.has_run ? &s->kiosk.run : NULL;
        const char *cur = "";
        if (run) {
            if (run->step_index < 0) {
                cur = run->next_station[0] ? run->next_station : run->current_station;
            } else {
                cur = run->current_station;
            }
        }
        set_lab_fit(g_w.run_title, cur[0] ? station_title(s, cur) : "--");
        if (!run) {
            set_lab(g_w.run_count, "--:--");
            set_lab(g_w.run_step, "Step --");
            set_lab_fit(g_w.run_next, "Next: --");
        } else if (run->step_index < 0) {
            set_lab(g_w.run_count, "--:--");
            set_lab(g_w.run_step, "Starting");
            if (run->next_station[0]) {
                snprintf(b, sizeof b, "Next: %s", station_title(s, run->next_station));
            } else {
                snprintf(b, sizeof b, "Next: --");
            }
            set_lab_fit(g_w.run_next, b);
        } else {
            zk_fmt_mmss(run->step_remaining_sec, a, sizeof a);
            set_lab(g_w.run_count, a);
            if (run->step_count > 0) {
                zk_fmt_step(run->step_index, run->step_count, a, sizeof a);
            } else {
                snprintf(a, sizeof a, "Step --");
            }
            set_lab(g_w.run_step, a);
            if (run->next_station[0]) {
                snprintf(b, sizeof b, "Next: %s", station_title(s, run->next_station));
            } else {
                snprintf(b, sizeof b, "Next: --");
            }
            set_lab_fit(g_w.run_next, b);
        }
        {
            int fw = 0;
            int planned;
            if (run && run->step_index >= 0) {
                planned = run->step_elapsed_sec + run->step_remaining_sec;
                if (planned > 0) {
                    fw = g_w.run_bar_w * run->step_elapsed_sec / planned;
                }
            }
            if (g_w.run_fill) {
                lv_obj_set_width(g_w.run_fill, fw);
            }
        }
    } else if (g_key.content == ZK_SCREEN_PAUSED) {
        if (s->have_kiosk) {
            zk_pause_title(&s->kiosk.pause, a, sizeof a);
            zk_pause_until_text(&s->kiosk.pause, s->wall, b, sizeof b);
        } else {
            snprintf(a, sizeof a, "PAUSED");
            snprintf(b, sizeof b, "--");
        }
        set_lab_fit(g_w.ban_title, a);
        set_lab_fit(g_w.ban_until, b);
        if (g_w.rain_bold && s->have_kiosk) {
            rain_parts(&s->kiosk.rain_strip, a, sizeof a, b, sizeof b);
            apply_rain(a, b);
        }
        if (s->have_kiosk && s->kiosk.has_next_effective) {
            zk_str_trim_copy(c, sizeof c, s->kiosk.next_effective.name);
            zk_fmt_fire_when(&s->kiosk.next_effective, s->wall, b, sizeof b);
            snprintf(a, sizeof a, "%s " MIDDOT " %s", c[0] ? c : "--", b);
            set_lab_fit(g_w.info_name, a);
            if (s->kiosk.has_next_run && s->kiosk.next_run.skipped_by_pause) {
                zk_str_trim_copy(c, sizeof c, s->kiosk.next_run.name);
                snprintf(a, sizeof a, "%s will be skipped", c[0] ? c : "Next run");
                set_lab_fit(g_w.info_sub0, a);
                set_lab_fit(g_w.info_sub1, "");
            } else {
                set_lab_fit(g_w.info_sub0, "");
                set_lab_fit(g_w.info_sub1, "");
            }
        } else {
            set_lab_fit(g_w.info_name, "Runs again when you resume");
            if (s->have_kiosk && s->kiosk.has_next_run && s->kiosk.next_run.skipped_by_pause) {
                zk_str_trim_copy(c, sizeof c, s->kiosk.next_run.name);
                snprintf(a, sizeof a, "%s will be skipped", c[0] ? c : "Next run");
                set_lab_fit(g_w.info_sub0, a);
            } else {
                set_lab_fit(g_w.info_sub0, "");
            }
            set_lab_fit(g_w.info_sub1, "");
        }
    } else if (g_key.content == ZK_SCREEN_STATION) {
        apply_station_sheet(s);
    } else if (g_key.content == ZK_SCREEN_NEEDS_UPDATE) {
        if (g_w.nu_title) {
            set_lab(g_w.nu_title, "Controller needs an update");
        }
        if (g_w.nu_body) {
            set_lab_fit(g_w.nu_body, "Please update the controller.");
        }
    } else if (g_key.content == ZK_SCREEN_PICKER) {
        for (i = 0; i < 4; i++) {
            zk_pause_preview_t pv;
            memset(&pv, 0, sizeof pv);
            zk_pause_preview(s->have_schedules ? &s->schedules : NULL, s->wall, (zk_pause_kind_t)i, &pv);
            set_lab(g_w.chip_t[i], pv.primary);
            set_lab_fit(g_w.chip_s[i], pv.secondary);
            if (g_w.chip_t[i] && g_w.chip_s[i]) {
                lv_obj_update_layout(g_w.chip_t[i]);
                lv_obj_align_to(g_w.chip_s[i], g_w.chip_t[i], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
            }
        }
    } else if (g_key.content == ZK_SCREEN_SCHEDULES) {
        const zk_schedule_t *sch = NULL;
        int nsch = s->have_schedules ? s->schedules.n : 0;
        if (nsch > 1) {
            snprintf(a, sizeof a, "%d of %d", g_sched_i + 1, nsch);
            set_lab_fit(g_w.sched_count, a);
        } else {
            set_lab_fit(g_w.sched_count, "");
        }
        if (nsch > 0 && g_sched_i >= 0 && g_sched_i < nsch) {
            sch = &s->schedules.items[g_sched_i];
        }
        if (sch) {
            name_upper(sch, a, sizeof a);
            set_lab_fit(g_w.sched_name, a);
            sched_summary(sch, b, sizeof b);
            set_lab_fit(g_w.sched_sum, b);
            ends_about(sch, c, sizeof c);
            set_lab_fit(g_w.sched_ends2, c);
            for (i = 0; i < g_w.n_rows; i++) {
                const char *nm = "--";
                if (i < sch->n_steps) {
                    nm = station_title(s, sch->steps[i].station_id);
                    snprintf(a, sizeof a, "%d", sch->steps[i].minutes);
                } else {
                    snprintf(a, sizeof a, "--");
                }
                set_lab_fit(g_w.row_name[i], nm);
                set_lab(g_w.row_min[i], a);
            }
        }
    }

    if (g_key.modal == ZK_SCREEN_CONFIRM_STOP) {
        set_lab(g_w.m_title, "Stop all watering?");
        set_lab_fit(g_w.m_body, "Turns every valve off.");
        set_lab_fit(g_w.m_sub, "");
    } else if (g_key.modal == ZK_SCREEN_CONFIRM_PAUSE) {
        zk_pause_preview_t pv;
        int chip = g_pick_chip;
        memset(&pv, 0, sizeof pv);
        if (chip < 0 || chip > 3) {
            chip = 0;
        }
        zk_pause_preview(s->have_schedules ? &s->schedules : NULL, s->wall, (zk_pause_kind_t)chip, &pv);
        set_lab(g_w.m_title, "Pause watering?");
        set_lab_fit(g_w.m_body, pv.primary);
        set_lab_fit(g_w.m_sub, pv.secondary);
    }
}

static int g_ov_have_prev, g_ov_prev_stale, g_ov_prev_toast;
static char g_ov_prev_text[96];
static lv_obj_t *g_ov_prev_stale_obj, *g_ov_prev_toast_obj;

static void overlays_reset(void)
{
    g_ov_have_prev = 0;
    g_ov_prev_stale = 0;
    g_ov_prev_toast = 0;
    g_ov_prev_text[0] = 0;
    g_ov_prev_stale_obj = NULL;
    g_ov_prev_toast_obj = NULL;
}

/* Toast and stale pill. The removed step strip was the bottom 6 px;
 * the pill is 48 px tall with a 10 px gap, so this stays at 416. */
#define OVERLAY_Y (ZK_SCREEN_H - 6 - 10 - 48)

static void overlays(const zk_snapshot_t *s)
{
    /* Touch LVGL only when the overlay state changes: set_pos and
     * move_foreground invalidate the area and would redraw every tick.
     * After rebuild, LVGL may reuse object addresses, so rebuild clears
     * this cache via overlays_reset. */
    int show_toast = g_toast_text[0] && zk_platform_mono() < g_toast_until;
    int y = OVERLAY_Y;
    int stale = s->stale ? 1 : 0;
    if (!show_toast) {
        g_toast_text[0] = 0;
    }
    if (g_ov_have_prev && stale == g_ov_prev_stale && show_toast == g_ov_prev_toast &&
        strcmp(g_ov_prev_text, g_toast_text) == 0 && g_ov_prev_stale_obj == g_w.stale &&
        g_ov_prev_toast_obj == g_w.toast) {
        return;
    }
    g_ov_have_prev = 1;
    g_ov_prev_stale = stale;
    g_ov_prev_toast = show_toast;
    g_ov_prev_stale_obj = g_w.stale;
    g_ov_prev_toast_obj = g_w.toast;
    snprintf(g_ov_prev_text, sizeof g_ov_prev_text, "%s", g_toast_text);
    if (g_w.stale) {
        if (stale) {
            lv_obj_set_pos(g_w.stale, 28, y);
            lv_obj_remove_flag(g_w.stale, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_w.stale);
            y -= 56;
        } else {
            lv_obj_add_flag(g_w.stale, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_w.toast) {
        if (show_toast) {
            set_lab_fit(g_w.toast_lbl, g_toast_text);
            lv_obj_set_pos(g_w.toast, 28, y);
            lv_obj_remove_flag(g_w.toast, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_w.toast);
        } else {
            lv_obj_add_flag(g_w.toast, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void rebuild(const zk_snapshot_t *s)
{
    lv_obj_t *scr = lv_screen_active();
    zk_layout_in in;
    zk_layout_rects L;
    view_key key = g_key;
    tracks_reset();
    overlays_reset();
    lv_obj_clean(scr);
    memset(&g_w, 0, sizeof g_w);
    fill_key(s, key.content, key.modal, &g_key, &in);
    zk_layout_rects_of((enum zk_screen)g_key.content, &in, &L);
    if (g_key.content != ZK_SCREEN_CONFIRM_STOP && g_key.content != ZK_SCREEN_CONFIRM_PAUSE) {
        build_rail(scr, &L, g_key.content);
        if (g_key.content == ZK_SCREEN_HOME) {
            build_home(scr, &L, s, &g_key);
        } else if (g_key.content == ZK_SCREEN_RUNNING) {
            build_running(scr, &L);
        } else if (g_key.content == ZK_SCREEN_PAUSED) {
            build_paused(scr, &L, &g_key);
        } else if (g_key.content == ZK_SCREEN_PICKER) {
            build_picker(scr, &L);
        } else if (g_key.content == ZK_SCREEN_SCHEDULES) {
            build_schedules(scr, &L, s, &g_key);
        } else if (g_key.content == ZK_SCREEN_STATION) {
            build_station(scr, &L);
        } else if (g_key.content == ZK_SCREEN_NEEDS_UPDATE) {
            build_needs_update(scr, &L);
        }
    }
    build_overlays(scr);
    if (g_key.modal == ZK_SCREEN_CONFIRM_STOP || g_key.modal == ZK_SCREEN_CONFIRM_PAUSE) {
        build_modal(scr, g_key.modal);
    }
    apply_content(s);
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

void zk_ui_debug_set_view(int screen, int pressed_target, int pick_chip)
{
    g_override = screen;
    g_pressed = pressed_target;
    g_pick_chip = pick_chip;
    g_toast_text[0] = 0;
    if (screen == ZK_SCREEN_CONFIRM_STOP || screen == ZK_SCREEN_CONFIRM_PAUSE) {
        g_modal = screen;
        g_nav = -1;
    } else if (screen >= 0) {
        g_modal = 0;
        if (screen == ZK_SCREEN_PICKER || screen == ZK_SCREEN_SCHEDULES ||
            screen == ZK_SCREEN_STATION) {
            g_nav = screen;
            if (screen == ZK_SCREEN_STATION) {
                g_station_i = pick_chip;
                g_station_id[0] = 0;
            }
        } else {
            g_nav = -1;
        }
    } else {
        g_modal = 0;
        g_nav = -1;
    }
    g_dirty = 1;
}

void zk_ui_tick(void)
{
    zk_snapshot_t snap;
    view_key key;
    zk_layout_in in;
    int content;
    int modal;
    zk_data_get(&snap);
    resolve(&snap, &content, &modal);
    fill_key(&snap, content, modal, &key, &in);
    g_lin = in;
    g_lin_ready = 1;
    g_hit_content = content;
    g_hit_modal = modal;
    if (!g_have_key || g_dirty || memcmp(&key, &g_key, sizeof key) != 0) {
        g_key = key;
        g_have_key = 1;
        g_dirty = 0;
        rebuild(&snap);
    } else if (snap.version != g_ver) {
        apply_content(&snap);
    }
    g_ver = snap.version;
    note_result();
    overlays(&snap);
}

void zk_ui_refresh(void)
{
    g_dirty = 1;
    zk_ui_tick();
    if (g_pressed > 0 && g_pressed < HIT_N && g_w.hit[g_pressed]) {
        lv_obj_add_state(g_w.hit[g_pressed], LV_STATE_PRESSED);
        if (g_pressed == ZK_TARGET_STOP && g_w.shadow) {
            lv_obj_add_flag(g_w.shadow, LV_OBJ_FLAG_HIDDEN);
        }
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
    g_override = -1;
    g_nav = -1;
    g_modal = 0;
    g_quiet_until = 0;
    g_pick_chip = -1;
    g_station_i = -1;
    g_station_id[0] = 0;
    g_pressed = 0;
    g_have_key = 0;
    g_lin_ready = 0;
    g_hit_content = 0;
    g_hit_modal = 0;
    g_dirty = 1;
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    zk_ui_refresh();
}

int zk_ui_hit_is_stop(int x, int y)
{
    enum zk_screen screen;

    if (!g_have_key || !g_lin_ready) {
        return 0;
    }
    if (g_hit_modal == ZK_SCREEN_CONFIRM_STOP || g_hit_modal == ZK_SCREEN_CONFIRM_PAUSE) {
        screen = (enum zk_screen)g_hit_modal;
    } else {
        screen = (enum zk_screen)g_hit_content;
    }
    return zk_layout_hit_is_stop(screen, &g_lin, x, y);
}

void zk_ui_power_inputs(zk_power_inputs_t *out)
{
    zk_snapshot_t snap;

    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    zk_data_get(&snap);
    if (!snap.have_kiosk || snap.api_state != ZK_API_OK || snap.stale) {
        out->unreachable = 1;
    }
    if (snap.have_kiosk) {
        out->watering = (snap.kiosk.watering || snap.kiosk.has_run || snap.kiosk.current_station[0] ||
                         snap.kiosk.n_on > 0)
                            ? 1
                            : 0;
        out->lockout = snap.kiosk.lockout ? 1 : 0;
        out->fault = (snap.kiosk.fault || snap.kiosk.last_error[0] || strcmp(snap.kiosk.phase, "Fault") == 0) ? 1
                                                                                                              : 0;
        out->paused = snap.kiosk.pause.paused ? 1 : 0;
    }
    if (g_hit_modal == ZK_SCREEN_CONFIRM_STOP || g_hit_modal == ZK_SCREEN_CONFIRM_PAUSE) {
        out->modal_open = 1;
    }
}
