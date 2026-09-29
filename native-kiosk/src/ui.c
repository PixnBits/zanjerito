#include "ui.h"

#include "data.h"
#include "platform.h"
#include "zk_layout.h"
#include "zk_logic.h"

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
#define ELLIPSIS "\xE2\x80\xA6"

enum { HIT_N = 24, ACT_SCHED_BODY = 1000 };

typedef struct {
    int content;
    int modal;
    int rain;
    int fault;
    int n_st;
    int soil_mask;
    int steps;
    int sched_i;
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
} widgets_t;

static zk_app_t g_app;
static char g_fix[512];
static lv_display_t *g_disp;
static widgets_t g_w;
static view_key g_key;
static int g_have_key;
static int g_dirty;
static int g_override = -1;
static int g_nav = -1;
static int g_modal;
static int g_pressed;
static int g_pick_chip = -1;
static int g_sched_i;
static uint32_t g_ver;
static uint32_t g_seen_seq;
static int g_seen_ok;
static char g_toast_text[96];
static double g_toast_until;

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
    return lb;
}

static void lab_width(lv_obj_t *lb, int w, lv_text_align_t align)
{
    lv_obj_set_width(lb, w);
    lv_obj_set_style_text_align(lb, align, 0);
}

static int fault_on(const zk_snapshot_t *s)
{
    if (!s->have_status) {
        return 0;
    }
    if (s->status.fault || s->status.lockout) {
        return 1;
    }
    return s->status.last_error[0] ? 1 : 0;
}

static int base_screen(const zk_snapshot_t *s)
{
    if (s->have_status && s->status.watering) {
        return ZK_SCREEN_RUNNING;
    }
    if (s->have_status && s->status.paused) {
        return ZK_SCREEN_PAUSED;
    }
    return ZK_SCREEN_HOME;
}

static void resolve(const zk_snapshot_t *s, int *content, int *modal)
{
    int base = base_screen(s);
    if (g_override == ZK_SCREEN_CONFIRM_STOP || g_override == ZK_SCREEN_CONFIRM_PAUSE) {
        *modal = g_override;
        *content = base;
        if (g_nav == ZK_SCREEN_PICKER || g_nav == ZK_SCREEN_SCHEDULES) {
            *content = g_nav;
        }
        return;
    }
    if (g_override >= 0) {
        *modal = 0;
        *content = g_override;
        return;
    }
    *modal = g_modal;
    if (g_nav == ZK_SCREEN_PICKER || g_nav == ZK_SCREEN_SCHEDULES) {
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
    k->have = s->have_status ? 1 : 0;
    k->fault = fault_on(s);
    k->rain = (s->have_status && zk_rain_strip_visible(&s->status)) ? 1 : 0;
    nst = (s->have_stations && s->stations.n > 0) ? s->stations.n : 0;
    if (nst > 8) {
        nst = 8;
    }
    k->n_st = nst;
    if (s->have_stations && s->have_soil) {
        for (i = 0; i < nst; i++) {
            if (zk_soil_percent(&s->soil, s->stations.items[i].id) >= 0) {
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
    int i;
    if (!id || !id[0]) {
        return "";
    }
    if (s->have_stations) {
        for (i = 0; i < s->stations.n; i++) {
            if (strcmp(s->stations.items[i].id, id) == 0) {
                return s->stations.items[i].title;
            }
        }
    }
    return id;
}

static int station_on(const zk_status_t *st, const char *id)
{
    int i;
    if (!st || !id || !id[0]) {
        return 0;
    }
    if (st->current_station[0] && strcmp(st->current_station, id) == 0) {
        return 1;
    }
    for (i = 0; i < st->n_on; i++) {
        if (strcmp(st->stations_on[i], id) == 0) {
            return 1;
        }
    }
    return 0;
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

static void name_raw(const zk_schedule_t *sch, char *out, size_t cap)
{
    zk_str_trim_copy(out, cap, sch->note);
    if (!out[0]) {
        zk_str_trim_copy(out, cap, sch->id);
    }
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

static void rain_parts(const zk_status_t *st, char *bold, size_t cb, char *rest, size_t cr)
{
    double inches;
    int hours;
    char amt[16];
    if ((st->rain.total_24h + 1e-9) >= 0.05) {
        hours = 24;
        inches = st->rain.total_24h;
    } else {
        hours = 72;
        inches = st->rain.total_72h;
    }
    zk_fmt_inches(inches, amt, sizeof amt);
    snprintf(bold, cb, "Rain %s in", amt);
    snprintf(rest, cr, " in the last %d h", hours);
}

static void draw_steps(lv_obj_t *scr)
{
    int k;
    box_at(scr, 0, 474, ZK_SCREEN_W, 3, COL_STOP, 0);
    for (k = 0; k < 17; k++) {
        int x0 = k * 48;
        int x;
        int w;
        x = x0 + 6;
        w = 12;
        if (x >= ZK_SCREEN_W) {
            break;
        }
        if (x + w > ZK_SCREEN_W) {
            w = ZK_SCREEN_W - x;
        }
        if (w > 0) {
            box_at(scr, x, 477, w, 3, COL_STOP, 0);
        }
        x = x0 + 30;
        w = 12;
        if (x >= ZK_SCREEN_W) {
            continue;
        }
        if (x + w > ZK_SCREEN_W) {
            w = ZK_SCREEN_W - x;
        }
        if (w > 0) {
            box_at(scr, x, 477, w, 3, COL_STOP, 0);
        }
    }
}

static void show_toast(const char *msg)
{
    snprintf(g_toast_text, sizeof g_toast_text, "%s", msg ? msg : "");
    g_toast_until = zk_platform_mono() + 2.5;
    if (g_w.toast_lbl) {
        set_lab(g_w.toast_lbl, g_toast_text);
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
    } else {
        snprintf(buf, sizeof buf, "Failed (HTTP %d)", r.http_status);
        show_toast(buf);
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
        } else {
            zk_data_submit_action(ZK_ACT_STOP, NULL);
        }
        close_modal();
        note_result();
        break;
    default:
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
    if (content == ZK_SCREEN_HOME || content == ZK_SCREEN_RUNNING) {
        build_slot(scr, L->slot, ZK_TARGET_PAUSE, "Pause", &zk_icon_pause_44, COL_PAUSE, COL_WHITE, running);
    } else if (content == ZK_SCREEN_PAUSED) {
        build_slot(scr, L->slot, ZK_TARGET_RESUME, "Resume", &zk_icon_play_44, COL_TEAL, COL_WHITE, 0);
    } else {
        build_slot(scr, L->slot, ZK_TARGET_BACK, "Back", &zk_icon_back_44, COL_BACK, COL_INK, 0);
    }
}

static void build_home(lv_obj_t *scr, const zk_layout_rects *L, const zk_snapshot_t *s, const view_key *k)
{
    lv_obj_t *hdr;
    lv_obj_t *brand;
    int i;
    hdr = target(scr, L->header, ZK_TARGET_HEADER_NEXT, COL_BG, 0, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(hdr, 0, LV_STATE_PRESSED);
    g_w.hdr_kicker = lab(hdr, "", &zk_font_b_26, COL_MUT, 4, 6);
    lab_width(g_w.hdr_kicker, L->header.w - 160, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(g_w.hdr_kicker, 1, 0);
    g_w.hdr_big = lab(hdr, "", &zk_font_xb_50, COL_INK, 4, 38);
    lab_width(g_w.hdr_big, L->header.w - 160, LV_TEXT_ALIGN_LEFT);
    brand = box_at(hdr, L->header.w - 148, 0, 144, 96, COL_BG, 0);
    lv_obj_set_style_bg_opa(brand, LV_OPA_TRANSP, 0);
    icon_at(brand, &zk_icon_mark_44, COL_TEAL, (144 - 44) / 2, 0);
    {
        lv_obj_t *nm = lab(brand, "Zanjerito", &zk_font_alfa_26, COL_TEAL, 0, 46);
        lab_width(nm, 144, LV_TEXT_ALIGN_CENTER);
    }
    if (k->fault && L->rain.w > 0) {
        lv_obj_t *pill = box_rect(scr, L->rain, COL_STOP, 16);
        g_w.fault_lbl = lab(pill, "", &zk_font_b_26, COL_WHITE, 16, 14);
        lab_width(g_w.fault_lbl, L->rain.w - 32, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(g_w.fault_lbl, LV_LABEL_LONG_CLIP);
    } else if (k->rain && L->rain.w > 0) {
        lv_obj_t *strip = box_rect(scr, L->rain, COL_RAINBG, 16);
        icon_at(strip, &zk_icon_cloud_36, COL_RAINFG, 14, 11);
        g_w.rain_bold = lab(strip, "", &zk_font_xb_28, COL_RAINFG, 62, 14);
        g_w.rain_rest = lab(strip, "", &zk_font_sb_28, COL_RAINFG, 200, 14);
    }
    g_w.n_tiles = L->n_tiles;
    for (i = 0; i < L->n_tiles && i < 8; i++) {
        zk_rect tr = L->tiles[i];
        int soil = (k->soil_mask & (1 << i)) != 0;
        lv_obj_t *tile = target(scr, tr, ZK_TARGET_TILE0 + i, COL_CARD, 22, 1);
        int title_y = soil ? 10 : (tr.h - 36) / 2;
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
    g_w.run_step = lab(c, "", &zk_font_b_28, COL_WHITE, 24, card.h - 18 - 30);
    g_w.run_next = lab(c, "", &zk_font_b_28, COL_WHITE, card.w / 2, card.h - 18 - 30);
    lab_width(g_w.run_next, card.w / 2 - 24, LV_TEXT_ALIGN_RIGHT);
}

static void build_paused(lv_obj_t *scr, const zk_layout_rects *L, const view_key *k)
{
    lv_obj_t *ban;
    lv_obj_t *info;
    lv_obj_t *sub;
    if (L->banner.w <= 0) {
        return;
    }
    ban = box_rect(scr, L->banner, COL_PAUSE, 24);
    icon_at(ban, &zk_icon_cloud_64, COL_WHITE, 18, (L->banner.h - 64) / 2);
    g_w.ban_title = lab(ban, "", &zk_font_b_26, COL_WHITE, 18 + 64 + 16, 18);
    lv_obj_set_style_text_letter_space(g_w.ban_title, 1, 0);
    g_w.ban_until = lab(ban, "", &zk_font_xb_48, COL_WHITE, 18 + 64 + 16, 48);
    lab_width(g_w.ban_until, L->banner.w - 18 - 64 - 16 - 16, LV_TEXT_ALIGN_LEFT);
    if (k->rain && L->rain.w > 0) {
        lv_obj_t *strip = box_rect(scr, L->rain, COL_RAINBG, 16);
        icon_at(strip, &zk_icon_cloud_36, COL_RAINFG, 14, 11);
        g_w.rain_bold = lab(strip, "", &zk_font_xb_28, COL_RAINFG, 62, 14);
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
    g_w.info_name = lab(info, "", &zk_font_xb_36, COL_INK, 20, 52);
    lab_width(g_w.info_name, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(g_w.info_name, LV_LABEL_LONG_WRAP);
    sub = lab(info, "Runs again after you resume", &zk_font_sb_28, COL_MUT, 20, 112);
    lab_width(sub, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
    sub = lab(info, "or when the pause ends.", &zk_font_sb_28, COL_MUT, 20, 146);
    lab_width(sub, L->info.w - 40, LV_TEXT_ALIGN_LEFT);
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
        g_w.chip_t[i] = lab(c, "", &zk_font_xb_36, COL_INK, 16, 72);
        lab_width(g_w.chip_t[i], L->chips[i].w - 32, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(g_w.chip_t[i], LV_LABEL_LONG_WRAP);
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
    g_w.sched_count = lab(scr, "", &zk_font_sb_26, COL_MUT, L->title.x, L->title.y + 12);
    lab_width(g_w.sched_count, L->title.w - 4, LV_TEXT_ALIGN_RIGHT);
    card = box_rect(scr, L->card, COL_CARD, 26);
    border_on(card, 3);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_evt, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_SCHED_BODY);
    g_w.sched_name = lab(card, "", &zk_font_b_26, COL_MUT, 20, 8);
    lv_obj_set_style_text_letter_space(g_w.sched_name, 1, 0);
    g_w.sched_sum = lab(card, "", &zk_font_xb_50, COL_INK, 20, 32);
    lab_width(g_w.sched_sum, L->card.w - 40 - 170, LV_TEXT_ALIGN_LEFT);
    g_w.sched_ends1 = lab(card, "Ends about", &zk_font_b_28, COL_MUT, L->card.w - 20 - 168, 8);
    lab_width(g_w.sched_ends1, 168, LV_TEXT_ALIGN_RIGHT);
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
        g_w.row_name[i] = lab(card, "", &zk_font_b_34, COL_INK, 20, y + 6);
        lab_width(g_w.row_name[i], L->card.w - 20 - 160, LV_TEXT_ALIGN_LEFT);
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
    g_w.m_body = lab(card, "", &zk_font_sb_28, COL_INK, 24, 84);
    lab_width(g_w.m_body, M.modal.w - 48, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(g_w.m_body, LV_LABEL_LONG_WRAP);
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
    g_w.toast_lbl = lab(g_w.toast, "", &zk_font_b_26, COL_CARD, 12, 10);
    lab_width(g_w.toast_lbl, 466, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(g_w.toast, LV_OBJ_FLAG_HIDDEN);
}

static void place_rain_rest(void)
{
    lv_point_t sz;
    const char *t;
    if (!g_w.rain_bold || !g_w.rain_rest) {
        return;
    }
    t = lv_label_get_text(g_w.rain_bold);
    lv_text_get_size(&sz, t ? t : "", &zk_font_xb_28, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(g_w.rain_rest, 62 + sz.x, 14);
}

static void apply_content(const zk_snapshot_t *s)
{
    char a[256];
    char b[128];
    char c[96];
    zk_next_run_t next;
    int i;

    if (g_key.content == ZK_SCREEN_HOME) {
        memset(&next, 0, sizeof next);
        if (s->have_status && s->have_schedules) {
            zk_next_run(&s->schedules, s->have_stations ? &s->stations : NULL, s->wall, &next);
        }
        if (!s->have_status) {
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " --");
            set_lab(g_w.hdr_kicker, a);
            set_lab(g_w.hdr_big, "Connecting" ELLIPSIS);
        } else if (!next.have) {
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " --");
            set_lab(g_w.hdr_kicker, a);
            set_lab(g_w.hdr_big, "--");
        } else {
            snprintf(a, sizeof a, "NEXT RUN " MIDDOT " %s", next.name);
            set_lab(g_w.hdr_kicker, a);
            snprintf(b, sizeof b, "%s %s", next.day, next.time);
            set_lab(g_w.hdr_big, b);
        }
        if (g_w.fault_lbl) {
            const char *err = (s->have_status && s->status.last_error[0]) ? s->status.last_error : "check controller";
            snprintf(a, sizeof a, "Fault: %s", err);
            set_lab(g_w.fault_lbl, a);
        }
        if (g_w.rain_bold && s->have_status) {
            rain_parts(&s->status, a, sizeof a, b, sizeof b);
            set_lab(g_w.rain_bold, a);
            set_lab(g_w.rain_rest, b);
            place_rain_rest();
        }
        for (i = 0; i < g_w.n_tiles; i++) {
            const zk_station_t *stn;
            int pct = -1;
            int low;
            int fill_w;
            if (!s->have_stations || i >= s->stations.n) {
                continue;
            }
            stn = &s->stations.items[i];
            set_lab(g_w.tile_title[i], stn->title);
            if (s->have_soil) {
                pct = zk_soil_percent(&s->soil, stn->id);
            }
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
                int on = s->have_status && station_on(&s->status, stn->id);
                if (on) {
                    lv_obj_remove_flag(g_w.tile_pill[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(g_w.tile_pill[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (g_key.content == ZK_SCREEN_RUNNING) {
        zk_run_info_t info;
        const char *cur = "";
        memset(&info, 0, sizeof info);
        info.remaining_sec = -1;
        if (s->have_status) {
            zk_run_infer(&s->status, s->have_schedules ? &s->schedules : NULL, s->wall, &info);
            cur = s->status.current_station;
            if (!cur[0] && s->status.n_on > 0) {
                cur = s->status.stations_on[0];
            }
        }
        set_lab(g_w.run_title, cur[0] ? station_title(s, cur) : "--");
        if (!info.have || info.remaining_sec < 0) {
            set_lab(g_w.run_count, "--:--");
        } else {
            zk_fmt_mmss(info.remaining_sec, a, sizeof a);
            set_lab(g_w.run_count, a);
        }
        if (info.have && info.step_count > 0) {
            zk_fmt_step(info.step_index, info.step_count, a, sizeof a);
        } else {
            snprintf(a, sizeof a, "Step --");
        }
        set_lab(g_w.run_step, a);
        if (info.next_station_id[0]) {
            snprintf(b, sizeof b, "Next: %s", station_title(s, info.next_station_id));
        } else {
            snprintf(b, sizeof b, "Next: --");
        }
        set_lab(g_w.run_next, b);
        {
            int fw = 0;
            if (info.total_step_sec > 0 && info.remaining_sec >= 0) {
                int elapsed = info.total_step_sec - info.remaining_sec;
                if (elapsed < 0) {
                    elapsed = 0;
                }
                if (elapsed > info.total_step_sec) {
                    elapsed = info.total_step_sec;
                }
                fw = g_w.run_bar_w * elapsed / info.total_step_sec;
            }
            if (g_w.run_fill) {
                lv_obj_set_width(g_w.run_fill, fw);
            }
        }
    } else if (g_key.content == ZK_SCREEN_PAUSED) {
        memset(&next, 0, sizeof next);
        if (s->have_status) {
            zk_pause_title(&s->status, a, sizeof a);
            zk_pause_until_text(&s->status, s->wall, b, sizeof b);
        } else {
            snprintf(a, sizeof a, "PAUSED");
            snprintf(b, sizeof b, "--");
        }
        set_lab(g_w.ban_title, a);
        set_lab(g_w.ban_until, b);
        if (g_w.rain_bold && s->have_status) {
            rain_parts(&s->status, a, sizeof a, b, sizeof b);
            set_lab(g_w.rain_bold, a);
            set_lab(g_w.rain_rest, b);
            place_rain_rest();
        }
        if (s->have_schedules) {
            zk_next_run(&s->schedules, s->have_stations ? &s->stations : NULL, s->wall, &next);
        }
        if (next.have && next.sched_index >= 0 && next.sched_index < s->schedules.n) {
            name_raw(&s->schedules.items[next.sched_index], c, sizeof c);
            snprintf(a, sizeof a, "%s " MIDDOT " %s", c, next.summary);
        } else {
            snprintf(a, sizeof a, "--");
        }
        set_lab(g_w.info_name, a);
    } else if (g_key.content == ZK_SCREEN_PICKER) {
        for (i = 0; i < 4; i++) {
            zk_pause_preview_t pv;
            memset(&pv, 0, sizeof pv);
            zk_pause_preview(s->have_schedules ? &s->schedules : NULL, s->wall, (zk_pause_kind_t)i, &pv);
            set_lab(g_w.chip_t[i], pv.primary);
            set_lab(g_w.chip_s[i], pv.secondary);
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
            set_lab(g_w.sched_count, a);
        } else {
            set_lab(g_w.sched_count, "");
        }
        if (nsch > 0 && g_sched_i >= 0 && g_sched_i < nsch) {
            sch = &s->schedules.items[g_sched_i];
        }
        if (sch) {
            name_upper(sch, a, sizeof a);
            set_lab(g_w.sched_name, a);
            sched_summary(sch, b, sizeof b);
            set_lab(g_w.sched_sum, b);
            ends_about(sch, c, sizeof c);
            set_lab(g_w.sched_ends2, c);
            for (i = 0; i < g_w.n_rows; i++) {
                const char *nm = "--";
                if (i < sch->n_steps) {
                    nm = station_title(s, sch->steps[i].station_id);
                    snprintf(a, sizeof a, "%d", sch->steps[i].minutes);
                } else {
                    snprintf(a, sizeof a, "--");
                }
                set_lab(g_w.row_name[i], nm);
                set_lab(g_w.row_min[i], a);
            }
        }
    }

    if (g_key.modal == ZK_SCREEN_CONFIRM_STOP) {
        set_lab(g_w.m_title, "Stop all watering?");
        set_lab(g_w.m_body, "Turns every valve off.");
        set_lab(g_w.m_sub, "");
    } else if (g_key.modal == ZK_SCREEN_CONFIRM_PAUSE) {
        zk_pause_preview_t pv;
        int chip = g_pick_chip;
        memset(&pv, 0, sizeof pv);
        if (chip < 0 || chip > 3) {
            chip = 0;
        }
        zk_pause_preview(s->have_schedules ? &s->schedules : NULL, s->wall, (zk_pause_kind_t)chip, &pv);
        set_lab(g_w.m_title, "Pause watering?");
        set_lab(g_w.m_body, pv.primary);
        set_lab(g_w.m_sub, pv.secondary);
    }
}

static void overlays(const zk_snapshot_t *s)
{
    /* Touch LVGL only when the overlay state changes: set_pos and
     * move_foreground invalidate the area and would redraw every tick. */
    static int have_prev, prev_stale, prev_toast;
    static char prev_text[sizeof g_toast_text];
    static lv_obj_t *prev_stale_obj, *prev_toast_obj;
    int show_toast = g_toast_text[0] && zk_platform_mono() < g_toast_until;
    int y = 474 - 10 - 48;
    int stale = s->stale ? 1 : 0;
    if (!show_toast) {
        g_toast_text[0] = 0;
    }
    if (have_prev && stale == prev_stale && show_toast == prev_toast &&
        strcmp(prev_text, g_toast_text) == 0 && prev_stale_obj == g_w.stale &&
        prev_toast_obj == g_w.toast) {
        return;
    }
    have_prev = 1;
    prev_stale = stale;
    prev_toast = show_toast;
    prev_stale_obj = g_w.stale;
    prev_toast_obj = g_w.toast;
    snprintf(prev_text, sizeof prev_text, "%s", g_toast_text);
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
            set_lab(g_w.toast_lbl, g_toast_text);
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
        }
    }
    build_overlays(scr);
    if (g_key.modal == ZK_SCREEN_CONFIRM_STOP || g_key.modal == ZK_SCREEN_CONFIRM_PAUSE) {
        build_modal(scr, g_key.modal);
    }
    draw_steps(scr);
    apply_content(s);
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
        if (screen == ZK_SCREEN_PICKER || screen == ZK_SCREEN_SCHEDULES) {
            g_nav = screen;
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
    g_pick_chip = -1;
    g_pressed = 0;
    g_have_key = 0;
    g_dirty = 1;
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    zk_ui_refresh();
}
