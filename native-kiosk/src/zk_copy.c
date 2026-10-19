#include "zk_copy.h"

#include "zk_logic.h"
#include "zk_text.h"

#include <stdio.h>
#include <string.h>

static const char *k_wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void zk_fmt_time_nbsp(int hh, int mm, char *out, size_t cap)
{
    const char *ampm;
    int h12;
    if (!out || cap == 0) {
        return;
    }
    if (hh < 0) {
        hh = 0;
    }
    if (hh > 23) {
        hh = 23;
    }
    if (mm < 0) {
        mm = 0;
    }
    if (mm > 59) {
        mm = 59;
    }
    ampm = (hh < 12) ? "AM" : "PM";
    h12 = hh % 12;
    if (h12 == 0) {
        h12 = 12;
    }
    snprintf(out, cap, "%d:%02d" ZK_NBSP "%s", h12, mm, ampm);
}

void zk_fmt_inches_trim(double v, char *out, size_t cap)
{
    int h;
    if (!out || cap == 0) {
        return;
    }
    if (!(v == v) || v < 0) {
        v = 0;
    }
    if (v > 999.99) {
        v = 999.99;
    }
    h = (int)(v * 100.0 + 0.5);
    if (h < 0) {
        h = 0;
    }
    if (h > 99999) {
        h = 99999;
    }
    if (h % 100 == 0) {
        snprintf(out, cap, "%d", h / 100);
    } else if (h % 10 == 0) {
        snprintf(out, cap, "%d.%d", h / 100, (h / 10) % 10);
    } else {
        snprintf(out, cap, "%d.%02d", h / 100, h % 100);
    }
}

static void clamp_to(char *dst, size_t cap, const char *src, int px, int lines, zk_width_fn fn, void *user)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        src = "";
    }
    if (!fn || px < 1 || lines < 1) {
        zk_str_copy(dst, cap, src);
        return;
    }
    if (zk_text_fit(dst, cap, src, px * lines, fn, user) != 0) {
        zk_str_copy(dst, cap, src);
    }
}

static int is_manual(const zk_kiosk_t *k)
{
    if (!k || !k->has_run) {
        return 0;
    }
    return strcmp(k->run.kind, "manual") == 0 || strcmp(k->run.program, "manual") == 0;
}

static int is_watering(const zk_kiosk_t *k)
{
    if (!k) {
        return 0;
    }
    return k->watering || k->has_run || k->n_on > 0 || k->current_station[0] ? 1 : 0;
}

static const zk_kiosk_station_t *station_at(const zk_kiosk_t *k, int index)
{
    if (!k || index < 0 || index >= k->n_stations) {
        return NULL;
    }
    return &k->stations[index];
}

static const zk_schedule_t *sched_by_id(const zk_schedules_t *sc, const char *id)
{
    int i;
    if (!sc || !id || !id[0]) {
        return NULL;
    }
    for (i = 0; i < sc->n; i++) {
        if (strcmp(sc->items[i].id, id) == 0) {
            return &sc->items[i];
        }
    }
    return NULL;
}

static int step_minutes_for(const zk_schedule_t *sch, const char *station_id)
{
    int i;
    if (!sch || !station_id) {
        return 0;
    }
    for (i = 0; i < sch->n_steps; i++) {
        if (strcmp(sch->steps[i].station_id, station_id) == 0 && sch->steps[i].minutes > 0) {
            return sch->steps[i].minutes;
        }
    }
    return 0;
}

int zk_usual_minutes(const zk_copy_src_t *src, int station_index)
{
    const zk_kiosk_station_t *st;
    const zk_schedule_t *sch;
    int i;
    int m = 0;
    if (!src || !src->kiosk) {
        return 10;
    }
    st = station_at(src->kiosk, station_index);
    if (!st) {
        return 10;
    }
    if (src->have_schedules && src->schedules && src->kiosk->has_next_run) {
        sch = sched_by_id(src->schedules, src->kiosk->next_run.schedule_id);
        m = step_minutes_for(sch, st->id);
    }
    if (m <= 0 && src->have_schedules && src->schedules) {
        for (i = 0; i < src->schedules->n; i++) {
            sch = &src->schedules->items[i];
            if (!sch->enabled) {
                continue;
            }
            m = step_minutes_for(sch, st->id);
            if (m > 0) {
                break;
            }
        }
    }
    if (m <= 0) {
        m = 10;
    }
    if (m > 60) {
        m = 60;
    }
    if (m < 1) {
        m = 1;
    }
    return m;
}

static void when_nbsp(zk_wall_t at, zk_wall_t now, char *out, size_t cap)
{
    char day[32];
    char t[24];
    zk_fmt_relative_day(at, now, day, sizeof day);
    zk_fmt_time_nbsp(at.hh, at.mm, t, sizeof t);
    snprintf(out, cap, "%s %s", day, t);
}

static void until_text(const zk_pause_t *p, char *out, size_t cap)
{
    char t[24];
    int w;
    if (!p || !p->has_until) {
        zk_str_copy(out, cap, "Until you resume");
        return;
    }
    w = p->until.wday;
    if (w < 0 || w > 6) {
        zk_wall_t tmp = p->until;
        zk_wall_set_wday(&tmp);
        w = tmp.wday;
    }
    if (w < 0 || w > 6) {
        w = 0;
    }
    zk_fmt_time_nbsp(p->until.hh, p->until.mm, t, sizeof t);
    snprintf(out, cap, "Until %s %s", k_wd[w], t);
}

static int weekday_bit(const zk_schedule_t *s)
{
    if (!s || s->weekdays == 0) {
        return 0x7F;
    }
    return (int)(s->weekdays & 0x7F);
}

static int range_overlap(const zk_schedule_t *a, const zk_schedule_t *b)
{
    int a0y = a->has_starts_on ? a->starts_y : 1;
    int a0m = a->has_starts_on ? a->starts_m : 1;
    int a0d = a->has_starts_on ? a->starts_d : 1;
    int a1y = a->has_ends_on ? a->ends_y : 9999;
    int a1m = a->has_ends_on ? a->ends_m : 12;
    int a1d = a->has_ends_on ? a->ends_d : 31;
    int b0y = b->has_starts_on ? b->starts_y : 1;
    int b0m = b->has_starts_on ? b->starts_m : 1;
    int b0d = b->has_starts_on ? b->starts_d : 1;
    int b1y = b->has_ends_on ? b->ends_y : 9999;
    int b1m = b->has_ends_on ? b->ends_m : 12;
    int b1d = b->has_ends_on ? b->ends_d : 31;
    if (!a->has_starts_on && !a->has_ends_on) {
        return 1;
    }
    if (!b->has_starts_on && !b->has_ends_on) {
        return 1;
    }
    if (zk_date_cmp(a0y, a0m, a0d, b1y, b1m, b1d) > 0) {
        return 0;
    }
    if (zk_date_cmp(b0y, b0m, b0d, a1y, a1m, a1d) > 0) {
        return 0;
    }
    return 1;
}

static int window_min(const zk_schedule_t *s, int *start, int *end)
{
    int i;
    int total = 0;
    if (!s || !s->enabled || !s->has_start || s->n_steps <= 0) {
        return 0;
    }
    for (i = 0; i < s->n_steps; i++) {
        if (s->steps[i].minutes > 0) {
            total += s->steps[i].minutes;
        }
    }
    if (total <= 0) {
        return 0;
    }
    *start = s->start_hh * 60 + s->start_mm;
    *end = *start + total;
    return 1;
}

static void sched_name(const zk_schedule_t *s, char *out, size_t cap)
{
    if (s->name[0]) {
        zk_str_copy(out, cap, s->name);
    } else {
        zk_str_copy(out, cap, s->id);
    }
}

typedef struct {
    int on;
    int wday;
    char earlier[ZK_NOTE_MAX];
    char later[ZK_NOTE_MAX];
    int start;
    int end;
    int later_start;
} overlap_t;

static int first_bit(int mask)
{
    int i;
    for (i = 0; i < 7; i++) {
        if (mask & (1 << i)) {
            return i;
        }
    }
    return 2;
}

static void find_overlap(const zk_copy_src_t *src, overlap_t *o)
{
    int i, j;
    int prefer = -1;
    memset(o, 0, sizeof *o);
    if (!src || !src->have_schedules || !src->schedules) {
        return;
    }
    if (src->kiosk && src->kiosk->has_next_run && src->kiosk->next_run.has_at) {
        prefer = src->kiosk->next_run.at.wday;
        if (prefer < 0 || prefer > 6) {
            zk_wall_t tmp = src->kiosk->next_run.at;
            zk_wall_set_wday(&tmp);
            prefer = tmp.wday;
        }
    }
    for (i = 0; i < src->schedules->n; i++) {
        int as, ae, amask;
        if (!window_min(&src->schedules->items[i], &as, &ae)) {
            continue;
        }
        amask = weekday_bit(&src->schedules->items[i]);
        for (j = i + 1; j < src->schedules->n; j++) {
            int bs, be, both, shared;
            if (!window_min(&src->schedules->items[j], &bs, &be)) {
                continue;
            }
            if (!range_overlap(&src->schedules->items[i], &src->schedules->items[j])) {
                continue;
            }
            both = amask & weekday_bit(&src->schedules->items[j]);
            if (!both) {
                continue;
            }
            if (!(as < be && bs < ae)) {
                continue;
            }
            shared = (prefer >= 0 && (both & (1 << prefer)) != 0) ? prefer : first_bit(both);
            o->on = 1;
            o->wday = shared;
            if (as <= bs) {
                sched_name(&src->schedules->items[i], o->earlier, sizeof o->earlier);
                sched_name(&src->schedules->items[j], o->later, sizeof o->later);
                o->start = as;
                o->end = ae;
                o->later_start = bs;
            } else {
                sched_name(&src->schedules->items[j], o->earlier, sizeof o->earlier);
                sched_name(&src->schedules->items[i], o->later, sizeof o->later);
                o->start = bs;
                o->end = be;
                o->later_start = as;
            }
            return;
        }
    }
}

static void min_to_nbsp(int minutes, char *out, size_t cap)
{
    int hh;
    int mm;
    if (minutes < 0) {
        minutes = 0;
    }
    hh = (minutes / 60) % 24;
    mm = minutes % 60;
    zk_fmt_time_nbsp(hh, mm, out, cap);
}

static int ceil_min(int sec)
{
    if (sec <= 0) {
        return 0;
    }
    return (sec + 59) / 60;
}

static const char *station_title_id(const zk_kiosk_t *k, const char *id)
{
    const zk_kiosk_station_t *st;
    if (!k || !id || !id[0]) {
        return "";
    }
    st = zk_kiosk_station(k, id);
    if (!st) {
        return "";
    }
    return st->title;
}

static int station_running(const zk_kiosk_t *k, const zk_kiosk_station_t *st)
{
    int i;
    if (!k || !st) {
        return 0;
    }
    if (st->on) {
        return 1;
    }
    if (k->current_station[0] && strcmp(k->current_station, st->id) == 0) {
        return 1;
    }
    if (k->has_run && strcmp(k->run.current_station, st->id) == 0) {
        return 1;
    }
    for (i = 0; i < k->n_on; i++) {
        if (strcmp(k->stations_on[i], st->id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int last_wday(const zk_copy_src_t *src, const char *id)
{
    int i;
    if (!src || !src->lasts || !id) {
        return -1;
    }
    for (i = 0; i < src->n_lasts; i++) {
        if (strcmp(src->lasts[i].id, id) == 0) {
            return src->lasts[i].wday;
        }
    }
    return -1;
}

static int soil_feature(const zk_copy_src_t *src)
{
    if (src->soil_doc) {
        return src->soil_doc->configured ? 1 : 0;
    }
    return src->kiosk && src->kiosk->soil.enabled ? 1 : 0;
}

static int age_days(const zk_copy_src_t *src, int *stale)
{
    const zk_wall_t *upd = NULL;
    int has = 0;
    int64_t a, b;
    int days;
    *stale = 0;
    if (src->soil_doc) {
        *stale = src->soil_doc->et_stale ? 1 : 0;
        if (src->soil_doc->has_updated) {
            upd = &src->soil_doc->updated_at;
            has = 1;
        }
    } else if (src->kiosk) {
        *stale = src->kiosk->soil.et_stale ? 1 : 0;
        if (src->kiosk->soil.has_updated_at) {
            upd = &src->kiosk->soil.updated_at;
            has = 1;
        }
    }
    if (!has || !upd) {
        return *stale ? 2 : 0;
    }
    a = zk_wall_epoch_sec(*upd);
    b = zk_wall_epoch_sec(src->wall);
    if (b < a) {
        days = 0;
    } else {
        days = (int)((b - a) / 86400);
    }
    if (days > 2) {
        *stale = 1;
    }
    if (*stale && days < 2) {
        days = 2;
    }
    return days;
}

static const zk_soil_zone_t *zone_for(const zk_soil_doc_t *doc, const char *id)
{
    int i;
    if (!doc || !id) {
        return NULL;
    }
    for (i = 0; i < doc->n_zones; i++) {
        if (strcmp(doc->zones[i].id, id) == 0) {
            return &doc->zones[i];
        }
    }
    return NULL;
}

static void fill_soil_text(zk_tile_copy_t *tile, const zk_copy_src_t *src, const zk_view_in_t *in,
                           const zk_kiosk_station_t *st, int feature)
{
    int stale = 0;
    int days = 0;
    int has = 0;
    int pct = 0;
    int rate = 0;
    const zk_soil_zone_t *z;
    tile->soil = ZK_SOIL_HIDDEN;
    tile->percent = -1;
    tile->muted_slot = 0;
    if (!feature) {
        return;
    }
    days = age_days(src, &stale);
    z = src->soil_doc ? zone_for(src->soil_doc, st->id) : NULL;
    if (z) {
        has = z->has_percent;
        pct = z->percent;
        rate = z->rate_measured;
    } else if (!src->soil_doc && st->soil_percent >= 0 && src->kiosk->soil.show_bars) {
        has = 1;
        pct = st->soil_percent;
        rate = 1;
    }
    if (!has || !rate) {
        tile->soil = ZK_SOIL_NONE;
        tile->muted_slot = 1;
        zk_str_copy(tile->slot, sizeof tile->slot, "Rate not measured");
        return;
    }
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    tile->percent = pct;
    if (z && z->has_age && z->age_days >= 2) {
        stale = 1;
        days = z->age_days;
    }
    if (stale) {
        tile->soil = ZK_SOIL_STALE;
        tile->muted_slot = 1;
        snprintf(tile->slot, sizeof tile->slot, "%d%% " ZK_MID " %dd old", pct, days);
        return;
    }
    tile->soil = ZK_SOIL_VALUE;
    snprintf(tile->slot, sizeof tile->slot, "%d%% " ZK_MID " %s", pct,
             zk_soil_word(in ? in->soil_cfg : NULL, st->id, pct));
}

/* Half up, away from zero. Negative, NaN, and non-positive return -1. */
static int half_up_units(double v, int scale)
{
    double x;
    if (!(v == v) || v <= 0 || scale < 1) {
        return -1;
    }
    if (v > 999.99) {
        v = 999.99;
    }
    x = v * (double)scale + 0.5;
    if (x >= 2000000000.0) {
        return 2000000000;
    }
    return (int)x;
}

static void fill_rain(zk_hero_copy_t *h, const zk_kiosk_t *k)
{
    int hundredths;
    int tenths;
    char num[24];
    double v;
    h->rain = 0;
    h->rain_text[0] = 0;
    h->rain_fill_px = 0;
    if (!k || !k->rain.have_totals) {
        return;
    }
    v = k->rain.total_72h;
    /* Show only when the 72 h total, rounded to 0.01 in, is at least 0.06 in.
     * The words round to 0.1 in, so the line never says "Rain 0.0 in". */
    hundredths = half_up_units(v, 100);
    if (hundredths < 6) {
        return;
    }
    tenths = half_up_units(v, 10);
    if (tenths < 1) {
        return;
    }
    snprintf(num, sizeof num, "%d.%d", tenths / 10, tenths % 10);
    snprintf(h->rain_text, sizeof h->rain_text, "Rain %s in " ZK_MID " last 72 h", num);
    h->rain = 1;
    if (v >= 1.0) {
        h->rain_fill_px = ZK_RAIN_TRACK;
    } else {
        h->rain_fill_px = (int)(v * (double)ZK_RAIN_TRACK + 0.5);
    }
}

static void count_line(const zk_copy_src_t *src, char *out, size_t cap)
{
    const zk_schedule_t *sch;
    int n = 0;
    int total = 0;
    int i;
    out[0] = 0;
    if (!src->kiosk || !src->kiosk->has_next_run) {
        return;
    }
    total = src->kiosk->next_run.total_min;
    sch = NULL;
    if (src->have_schedules && src->schedules) {
        sch = sched_by_id(src->schedules, src->kiosk->next_run.schedule_id);
    }
    if (sch) {
        n = sch->n_steps;
        if (total <= 0) {
            for (i = 0; i < sch->n_steps; i++) {
                total += sch->steps[i].minutes;
            }
        }
    }
    if (n <= 0 || total <= 0) {
        return;
    }
    snprintf(out, cap, "%d station%s " ZK_MID " about %d min", n, n == 1 ? "" : "s", total);
}

static const char *program_name(const zk_kiosk_t *k)
{
    if (k->has_run && k->run.program[0]) {
        return k->run.program;
    }
    if (k->has_next_run && k->next_run.name[0]) {
        return k->next_run.name;
    }
    return "";
}

static void upper_label(char *dst, size_t cap, const char *prefix, const char *name)
{
    char up[ZK_TITLE_MAX];
    zk_str_copy(up, sizeof up, name ? name : "");
    zk_str_upper(up);
    snprintf(dst, cap, "%s%s", prefix, up);
}

static void sched_days(unsigned m, char *out, size_t cap)
{
    static const char *wd[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const unsigned bit[7] = {ZK_WD_MON, ZK_WD_TUE, ZK_WD_WED, ZK_WD_THU, ZK_WD_FRI, ZK_WD_SAT, ZK_WD_SUN};
    unsigned known;
    size_t pos = 0;
    int i;
    int n = 0;
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    known = m & ZK_WD_ALL;
    if (m == 0 || known == ZK_WD_ALL) {
        zk_str_copy(out, cap, "Every day");
        return;
    }
    if (known == (ZK_WD_MON | ZK_WD_TUE | ZK_WD_WED | ZK_WD_THU | ZK_WD_FRI)) {
        zk_str_copy(out, cap, "Weekdays");
        return;
    }
    if (known == (ZK_WD_SAT | ZK_WD_SUN)) {
        zk_str_copy(out, cap, "Weekends");
        return;
    }
    for (i = 0; i < 7; i++) {
        int wr;
        if ((known & bit[i]) == 0) {
            continue;
        }
        wr = snprintf(out + pos, cap - pos, "%s%s", n ? " " : "", wd[i]);
        if (wr < 0 || (size_t)wr >= cap - pos) {
            out[cap - 1] = 0;
            return;
        }
        pos += (size_t)wr;
        n++;
    }
    if (!out[0]) {
        zk_str_copy(out, cap, "Every day");
    }
}

static void sched_disp_name(const zk_schedule_t *sch, char *out, size_t cap)
{
    char tmp[ZK_NOTE_MAX];
    zk_str_trim_copy(tmp, sizeof tmp, sch->name);
    if (!tmp[0]) {
        zk_str_trim_copy(tmp, sizeof tmp, sch->note);
    }
    if (!tmp[0]) {
        zk_str_trim_copy(tmp, sizeof tmp, sch->id);
    }
    zk_str_copy(out, cap, tmp);
}

static void fill_sched_row(const zk_schedule_t *sch, zk_sched_row_t *row)
{
    char days[64];
    char clk[24];
    const char *when;
    int steps;
    int mins = 0;
    int i;
    sched_disp_name(sch, row->name, sizeof row->name);
    sched_days(sch->weekdays, days, sizeof days);
    if (sch->has_start) {
        zk_fmt_time_nbsp(sch->start_hh, sch->start_mm, clk, sizeof clk);
        when = clk;
    } else {
        when = "No start";
    }
    steps = sch->n_steps;
    if (steps < 0) {
        steps = 0;
    }
    if (steps > ZK_MAX_STEPS) {
        steps = ZK_MAX_STEPS;
    }
    for (i = 0; i < steps; i++) {
        if (sch->steps[i].minutes > 0) {
            mins += sch->steps[i].minutes;
        }
    }
    snprintf(row->meta, sizeof row->meta, "%s " ZK_MID " %s " ZK_MID " %d %s " ZK_MID " %d min " ZK_MID " %s", days, when,
             steps, steps == 1 ? "step" : "steps", mins, sch->enabled ? "Enabled" : "Disabled");
    zk_utf8_seal(row->meta);
    row->enabled = sch->enabled ? 1 : 0;
    row->on = 1;
}

static void fill_sched(const zk_copy_src_t *src, const zk_view_in_t *in, zk_view_t *out)
{
    const zk_schedules_t *sc;
    zk_wall_t wall;
    char clk[24];
    int n;
    int start;
    int i;
    zk_str_copy(out->sched.note, sizeof out->sched.note, ZK_SCHED_NOTE);
    zk_str_copy(out->sched.empty_title, sizeof out->sched.empty_title, ZK_SCHED_EMPTY);
    zk_str_copy(out->sched.empty_body, sizeof out->sched.empty_body, ZK_SCHED_EMPTY_BODY);
    if (src->unreachable) {
        if (src->have_kiosk && src->kiosk && src->kiosk->has_now) {
            wall = src->kiosk->now;
        } else {
            wall = src->wall;
        }
        zk_fmt_time_nbsp(wall.hh, wall.mm, clk, sizeof clk);
        snprintf(out->sched.label, sizeof out->sched.label, "SCHEDULES " ZK_MID " LAST UPDATE %s", clk);
    } else {
        zk_str_copy(out->sched.label, sizeof out->sched.label, ZK_SCHED_READ);
    }
    sc = (src->have_schedules && src->schedules) ? src->schedules : NULL;
    n = sc ? sc->n : 0;
    if (n < 0) {
        n = 0;
    }
    if (n > ZK_MAX_SCHEDULES) {
        n = ZK_MAX_SCHEDULES;
    }
    start = in->sched_index;
    if (start < 0 || start >= n) {
        start = 0;
    }
    out->sched.n_total = n;
    out->sched.next_on = n > 2 ? 1 : 0;
    if (n > 2) {
        char base[96];
        int a = start + 1;
        int b = start + 2;
        if (b > n) {
            b = n;
        }
        zk_str_copy(base, sizeof base, out->sched.label);
        if (a == b) {
            snprintf(out->sched.label, sizeof out->sched.label, "%s " ZK_MID " %d of %d", base, a, n);
        } else {
            snprintf(out->sched.label, sizeof out->sched.label, "%s " ZK_MID " %d" ZK_EN "%d of %d", base, a, b, n);
        }
    }
    out->sched.n_rows = 0;
    for (i = 0; i < 2; i++) {
        int idx = start + i;
        if (!sc || idx >= n) {
            continue;
        }
        fill_sched_row(&sc->items[idx], &out->sched.rows[i]);
        out->sched.n_rows++;
    }
}

void zk_view_build(const zk_copy_src_t *src, const zk_view_in_t *in, zk_view_t *out)
{
    zk_copy_src_t empty_src;
    zk_view_in_t empty_in;
    const zk_kiosk_t *k;
    overlap_t ov;
    int offline;
    int manual;
    int watering;
    int paused;
    int confirmed;
    int pending;
    int i;
    int n;
    int feature;
    char tbuf[32];
    char when[64];
    if (!out) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (!src) {
        memset(&empty_src, 0, sizeof empty_src);
        src = &empty_src;
    }
    if (!in) {
        memset(&empty_in, 0, sizeof empty_in);
        in = &empty_in;
    }
    k = src->have_kiosk ? src->kiosk : NULL;
    offline = src->unreachable || !k;
    manual = k && is_manual(k);
    watering = k && is_watering(k);
    paused = k && k->pause.paused;
    confirmed = in->stop_phase == ZK_STOP_CONFIRMED && !manual && !watering;
    /* Pending from the moment Stop all is confirmed, including the first POST. */
    pending = !confirmed && (in->stop_phase == ZK_STOP_QUEUED || in->stop_phase == ZK_STOP_WAIT);
    find_overlap(src, &ov);
    out->overlap = (!offline && !manual && !watering && !paused && k && k->has_next_run && ov.on) ? 1 : 0;
    if (ov.on && ov.wday >= 0 && ov.wday <= 6) {
        char a[24], b[24], c[24];
        min_to_nbsp(ov.start, a, sizeof a);
        min_to_nbsp(ov.end, b, sizeof b);
        min_to_nbsp(ov.later_start, c, sizeof c);
        snprintf(out->overlap_title, sizeof out->overlap_title, "Two programs overlap on %s", k_wd[ov.wday]);
        snprintf(out->overlap_body, sizeof out->overlap_body,
                 "%s (%s " ZK_EN " %s) overlaps %s (%s). Stations would wait their turn. "
                 "Fix on phone: Zanjerito " ZK_RQ " Schedules.",
                 ov.earlier, a, b, ov.later, c);
    }
    fill_rain(&out->hero, k);
    if (confirmed) {
        zk_wall_t at = in->has_confirmed_at ? in->confirmed_at : src->wall;
        zk_fmt_time_nbsp(at.hh, at.mm, tbuf, sizeof tbuf);
        snprintf(out->hero.label, sizeof out->hero.label, "STOP " ZK_MID " CONFIRMED %s", tbuf);
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, "Watering stopped");
        if (paused) {
            if (k && k->has_next_effective && k->next_effective.has_at) {
                when_nbsp(k->next_effective.at, src->wall, when, sizeof when);
                snprintf(out->hero.secondary, sizeof out->hero.secondary, "Next run %s", when);
            } else {
                zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, "Paused until you resume");
            }
        } else if (k && k->has_next_run && k->next_run.has_at) {
            when_nbsp(k->next_run.at, src->wall, when, sizeof when);
            snprintf(out->hero.secondary, sizeof out->hero.secondary, "Next run %s", when);
        }
        out->hero.style = ZK_HERO_STOP_CONFIRMED;
    } else if (pending) {
        /* Same two-line headline before and after a 2xx. Only the label and line 2 change. */
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, ZK_STOP_PENDING_HEADLINE);
        if (offline) {
            zk_fmt_time_nbsp(k ? k->now.hh : src->wall.hh, k ? k->now.mm : src->wall.mm, tbuf, sizeof tbuf);
            snprintf(out->hero.label, sizeof out->hero.label, "STOP " ZK_MID " LAST UPDATE %s", tbuf);
            zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, ZK_STOP_PENDING_OFFLINE);
        } else {
            zk_str_copy(out->hero.label, sizeof out->hero.label, ZK_STOP_PENDING_LABEL);
            zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, ZK_STOP_PENDING_RETRY);
        }
        out->hero.style = ZK_HERO_STOP_PENDING;
    } else if (offline) {
        if (k && k->has_now) {
            zk_fmt_time_nbsp(k->now.hh, k->now.mm, tbuf, sizeof tbuf);
        } else {
            zk_fmt_time_nbsp(src->wall.hh, src->wall.mm, tbuf, sizeof tbuf);
        }
        snprintf(out->hero.label, sizeof out->hero.label, "LAST UPDATE %s " ZK_MID " RETRYING", tbuf);
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, "Can't reach controller");
        zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, "Check the controller power and Wi-Fi");
        out->hero.style = ZK_HERO_OFFLINE;
    } else if (manual) {
        const char *title = station_title_id(k, k->run.current_station);
        int left = ceil_min(k->run.step_remaining_sec);
        zk_wall_t end = src->wall;
        if (!title[0]) {
            title = station_title_id(k, k->current_station);
        }
        upper_label(out->hero.label, sizeof out->hero.label, "MANUAL RUN " ZK_MID " ", title);
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, title);
        snprintf(out->hero.time_line, sizeof out->hero.time_line, "%d min left", left);
        zk_wall_add_seconds(&end, k->run.step_remaining_sec > 0 ? k->run.step_remaining_sec : 0);
        zk_fmt_time_nbsp(end.hh, end.mm, tbuf, sizeof tbuf);
        snprintf(out->hero.secondary, sizeof out->hero.secondary, "ends %s", tbuf);
        out->hero.style = ZK_HERO_MANUAL;
    } else if (watering) {
        const char *title = station_title_id(k, k->has_run ? k->run.current_station : k->current_station);
        const char *next = k->has_run ? station_title_id(k, k->run.next_station) : "";
        int left = k->has_run ? ceil_min(k->run.step_remaining_sec) : 0;
        zk_wall_t end = src->wall;
        char prog[ZK_NOTE_MAX];
        zk_str_copy(prog, sizeof prog, program_name(k));
        upper_label(out->hero.label, sizeof out->hero.label, "WATERING NOW " ZK_MID " ", prog);
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, title[0] ? title : prog);
        snprintf(out->hero.time_line, sizeof out->hero.time_line, "%d min left", left);
        if (k->has_run) {
            zk_wall_add_seconds(&end, k->run.step_remaining_sec > 0 ? k->run.step_remaining_sec : 0);
        }
        zk_fmt_time_nbsp(end.hh, end.mm, tbuf, sizeof tbuf);
        if (next[0]) {
            snprintf(out->hero.secondary, sizeof out->hero.secondary, "ends %s " ZK_MID " next %s", tbuf, next);
        } else {
            snprintf(out->hero.secondary, sizeof out->hero.secondary, "ends %s", tbuf);
        }
        out->hero.style = ZK_HERO_WATERING;
    } else if (paused) {
        zk_str_copy(out->hero.label, sizeof out->hero.label, "PAUSED FOR RAIN");
        until_text(&k->pause, out->hero.headline, sizeof out->hero.headline);
        /* Spec row only. The next fire stays off this hero; "Watering stopped"
         * while paused uses next_effective_run instead. */
        zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, "Tap a station to run it anyway");
        out->hero.style = ZK_HERO_PAUSED;
    } else if (!k || !k->has_next_run) {
        zk_str_copy(out->hero.label, sizeof out->hero.label, "SCHEDULE");
        zk_str_copy(out->hero.headline, sizeof out->hero.headline, "Nothing scheduled");
        zk_str_copy(out->hero.secondary, sizeof out->hero.secondary, "Add a program on your phone");
        out->hero.style = ZK_HERO_NOTHING;
    } else if (out->overlap) {
        char prog[ZK_NOTE_MAX];
        zk_str_copy(prog, sizeof prog, k->next_run.name);
        upper_label(out->hero.label, sizeof out->hero.label, "NEXT RUN " ZK_MID " ", prog);
        if (k->next_run.has_at) {
            when_nbsp(k->next_run.at, src->wall, out->hero.headline, sizeof out->hero.headline);
        }
        snprintf(out->hero.secondary, sizeof out->hero.secondary, "Overlap on %s " ZK_MID " Fix on phone",
                 k_wd[ov.wday >= 0 && ov.wday <= 6 ? ov.wday : 0]);
        out->hero.style = ZK_HERO_OVERLAP;
    } else {
        char prog[ZK_NOTE_MAX];
        zk_str_copy(prog, sizeof prog, k->next_run.name);
        upper_label(out->hero.label, sizeof out->hero.label, "NEXT RUN " ZK_MID " ", prog);
        if (k->next_run.has_at) {
            when_nbsp(k->next_run.at, src->wall, out->hero.headline, sizeof out->hero.headline);
        }
        count_line(src, out->hero.secondary, sizeof out->hero.secondary);
        out->hero.style = ZK_HERO_IDLE;
    }
    {
        char tmp[160];
        zk_str_copy(tmp, sizeof tmp, out->hero.label);
        clamp_to(out->hero.label, sizeof out->hero.label, tmp, in->label_px, 2, in->width_px, in->width_user);
    }

    n = k ? zk_layout_shown(k->n_stations) : 0;
    out->n_tiles = n;
    feature = k ? soil_feature(src) : 0;
    for (i = 0; i < n; i++) {
        const zk_kiosk_station_t *st = &k->stations[i];
        int lines = n > 4 ? 2 : 3;
        int remain = -1;
        {
            char tmp[ZK_TITLE_MAX];
            zk_str_copy(tmp, sizeof tmp, st->title);
            clamp_to(out->tiles[i].name, sizeof out->tiles[i].name, tmp, in->tile_px, lines, in->width_px,
                     in->width_user);
        }
        if (offline || pending) {
            out->tiles[i].vis = ZK_TILE_UNAVAILABLE;
            zk_str_copy(out->tiles[i].slot, sizeof out->tiles[i].slot, "unavailable");
            out->tiles[i].muted_slot = 1;
            out->tiles[i].soil = ZK_SOIL_HIDDEN;
            out->tiles[i].percent = -1;
            continue;
        }
        if (station_running(k, st)) {
            out->tiles[i].vis = ZK_TILE_ON;
        } else {
            out->tiles[i].vis = ZK_TILE_OFF;
        }
        if (k->has_run && strcmp(k->run.current_station, st->id) == 0) {
            remain = ceil_min(k->run.step_remaining_sec);
        }
        if (out->tiles[i].vis == ZK_TILE_ON && remain >= 0) {
            snprintf(out->tiles[i].slot, sizeof out->tiles[i].slot, "%d min left", remain);
            out->tiles[i].soil = ZK_SOIL_HIDDEN;
            out->tiles[i].percent = -1;
        } else if (feature) {
            fill_soil_text(&out->tiles[i], src, in, st, 1);
        } else {
            int wd = last_wday(src, st->id);
            out->tiles[i].soil = ZK_SOIL_HIDDEN;
            out->tiles[i].percent = -1;
            if (wd >= 0 && wd <= 6) {
                snprintf(out->tiles[i].slot, sizeof out->tiles[i].slot, "last %s", k_wd[wd]);
            }
            out->tiles[i].muted_slot = 1;
        }
    }
    out->bar_disabled = (offline || pending) ? 1 : 0;
    out->bar_is_resume = paused ? 1 : 0;
    zk_str_copy(out->bar_word, sizeof out->bar_word, paused ? ZK_WORD_RESUME : ZK_WORD_PAUSE);
    /* Filled while water is on, and while a stop is still unacknowledged.
     * Outline only after the controller acks and nothing is on. */
    out->stop_filled = (pending || ((watering || manual) && !confirmed)) ? 1 : 0;

    out->sheet_kind = in->sheet_kind;
    if (in->sheet_kind == ZK_SHEET_RUN || in->sheet_kind == ZK_SHEET_ANYWAY) {
        const zk_kiosk_station_t *st = k ? station_at(k, in->station_index) : NULL;
        int minutes = in->minutes;
        if (minutes < 1) {
            minutes = 1;
        }
        if (minutes > 60) {
            minutes = 60;
        }
        out->sheet.minutes = minutes;
        out->sheet.show_stepper = 1;
        out->sheet.minus_enabled = minutes > 1;
        if (in->sheet_kind == ZK_SHEET_ANYWAY) {
            char until[80];
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "PAUSED FOR RAIN");
            snprintf(out->sheet.title, sizeof out->sheet.title, "Run %s anyway?", st ? st->title : "");
            if (k && k->pause.has_until) {
                char clk[24];
                int w = k->pause.until.wday;
                if (w < 0 || w > 6) {
                    zk_wall_t tmp = k->pause.until;
                    zk_wall_set_wday(&tmp);
                    w = tmp.wday;
                }
                if (w < 0 || w > 6) {
                    w = 0;
                }
                zk_fmt_time_nbsp(k->pause.until.hh, k->pause.until.mm, clk, sizeof clk);
                snprintf(until, sizeof until, "%s %s", k_wd[w], clk);
            } else {
                zk_str_copy(until, sizeof until, "you resume");
            }
            snprintf(out->sheet.body, sizeof out->sheet.body,
                     "Paused until %s. Runs once; the pause stays on.", until);
        } else {
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "RUN A STATION");
            snprintf(out->sheet.title, sizeof out->sheet.title, "Run %s for %d min?", st ? st->title : "", minutes);
            zk_str_copy(out->sheet.body, sizeof out->sheet.body, "Runs once now. Schedules don't change.");
        }
    } else if (in->sheet_kind == ZK_SHEET_STOP_STATION) {
        const zk_kiosk_station_t *st = k ? station_at(k, in->station_index) : NULL;
        int manual_run = k && is_manual(k);
        const char *next = (k && k->has_run) ? station_title_id(k, k->run.next_station) : "";
        int left = (k && k->has_run) ? ceil_min(k->run.step_remaining_sec) : 0;
        int remain = (k && k->has_run && k->run.step_remaining_sec > 0) ? k->run.step_remaining_sec : 0;
        zk_wall_t end = src->wall;
        char clk[24];
        char chosen[480];
        if (manual_run) {
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "MANUAL RUN");
        } else if (k) {
            upper_label(out->sheet.label, sizeof out->sheet.label, "WATERING NOW " ZK_MID " ", program_name(k));
        } else {
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "WATERING NOW");
        }
        snprintf(out->sheet.title, sizeof out->sheet.title, "%s is watering", st ? st->title : "");
        zk_wall_add_seconds(&end, remain);
        zk_fmt_time_nbsp(end.hh, end.mm, clk, sizeof clk);
        snprintf(out->sheet.time_line, sizeof out->sheet.time_line, "%d min left " ZK_MID " ends %s", left, clk);
        if (manual_run) {
            zk_str_copy(chosen, sizeof chosen, "Manual run. Tap STOP below to end it now.");
        } else if (next[0]) {
            snprintf(chosen, sizeof chosen, "Next: %s. Tap STOP below to end all watering.", next);
        } else {
            zk_str_copy(chosen, sizeof chosen, "Last station. Tap STOP below to end watering now.");
        }
        if (in->width_px && in->width_px(chosen, strlen(chosen), in->width_user) > 728) {
            zk_str_copy(chosen, sizeof chosen, "Tap STOP below to end all watering.");
        }
        zk_str_copy(out->sheet.body, sizeof out->sheet.body, chosen);
    } else if (in->sheet_kind == ZK_SHEET_STOP_ALL) {
        if (offline) {
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "STOP " ZK_MID " CAN'T REACH CONTROLLER");
            zk_str_copy(out->sheet.body, sizeof out->sheet.body,
                        "The controller can't be reached right now. The kiosk keeps trying to send the stop "
                        "and shows 'Watering stopped' only when the controller confirms it.");
        } else {
            const char *title = "";
            const char *prog = "";
            int manual_run = k && is_manual(k);
            zk_str_copy(out->sheet.label, sizeof out->sheet.label, "STOP");
            if (k && k->has_run) {
                title = station_title_id(k, k->run.current_station);
                prog = k->run.program;
            } else if (k && k->current_station[0]) {
                title = station_title_id(k, k->current_station);
            }
            if (manual_run) {
                if (title[0]) {
                    snprintf(out->sheet.body, sizeof out->sheet.body,
                             "Ends the manual run on %s now. Schedules stay on.", title);
                } else {
                    zk_str_copy(out->sheet.body, sizeof out->sheet.body,
                                "Ends the manual run now. Schedules stay on.");
                }
            } else if (title[0] && prog[0]) {
                snprintf(out->sheet.body, sizeof out->sheet.body,
                         "%s closes now and the rest of %s is skipped. Schedules stay on.", title, prog);
            } else if (title[0]) {
                snprintf(out->sheet.body, sizeof out->sheet.body,
                         "%s closes now and the rest of the program is skipped. Schedules stay on.", title);
            } else {
                zk_str_copy(out->sheet.body, sizeof out->sheet.body,
                            "Nothing is watering right now. Schedules stay on.");
            }
        }
        zk_str_copy(out->sheet.title, sizeof out->sheet.title, "Stop all watering?");
    } else if (in->sheet_kind == ZK_SHEET_OVERLAP) {
        zk_str_copy(out->sheet.label, sizeof out->sheet.label, "OVERLAP " ZK_MID " READ ONLY");
        zk_str_copy(out->sheet.title, sizeof out->sheet.title,
                    out->overlap_title[0] ? out->overlap_title : "Two programs overlap");
        zk_str_copy(out->sheet.body, sizeof out->sheet.body, out->overlap_body);
    }
    fill_sched(src, in, out);
}
