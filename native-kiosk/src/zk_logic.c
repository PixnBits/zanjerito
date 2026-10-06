#include "zk_logic.h"

#include <stdio.h>
#include <string.h>

static const char *k_wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *k_mon[13] = {
    "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void zk_fmt_time12(int hh, int mm, char *out, size_t cap)
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
    snprintf(out, cap, "%d:%02d %s", h12, mm, ampm);
}

void zk_fmt_mmss(int sec, char *out, size_t cap)
{
    int m, s;
    if (!out || cap == 0) {
        return;
    }
    if (sec < 0) {
        sec = 0;
    }
    m = sec / 60;
    s = sec % 60;
    snprintf(out, cap, "%d:%02d", m, s);
}

void zk_fmt_step(int step_index0, int step_count, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    snprintf(out, cap, "Step %d of %d", step_index0 + 1, step_count);
}

void zk_fmt_inches(double v, char *out, size_t cap)
{
    double scaled;
    double r;
    int hundredths;
    char s[32];
    size_t n;
    if (!out || cap == 0) {
        return;
    }
    /* NaN, negatives, and -inf become 0. Do not cast those to int. */
    if (!(v == v) || v < 0) {
        v = 0;
    } else if (v > 999.99) {
        v = 999.99;
    }
    scaled = v * 100.0 + 0.5;
    if (scaled > 99999.0) {
        scaled = 99999.0;
    }
    hundredths = (int)scaled;
    if (hundredths < 0) {
        hundredths = 0;
    }
    if (hundredths > 99999) {
        hundredths = 99999;
    }
    r = (double)hundredths / 100.0;
    snprintf(s, sizeof(s), "%.2f", r);
    n = strlen(s);
    if (n > 0 && s[n - 1] == '0') {
        snprintf(s, sizeof(s), "%.1f", r);
    }
    zk_str_copy(out, cap, s);
}

void zk_fmt_relative_day(zk_wall_t at, zk_wall_t now, char *out, size_t cap)
{
    int i;
    if (!out || cap == 0) {
        return;
    }
    i = zk_days_from_civil(at.y, at.m, at.d) - zk_days_from_civil(now.y, now.m, now.d);
    if (i <= 0) {
        zk_str_copy(out, cap, "Today");
        return;
    }
    if (i == 1) {
        zk_str_copy(out, cap, "Tomorrow");
        return;
    }
    if (at.wday < 0 || at.wday > 6) {
        zk_wall_set_wday(&at);
    }
    if (i <= 6) {
        zk_str_copy(out, cap, k_wd[at.wday < 0 || at.wday > 6 ? 0 : at.wday]);
        return;
    }
    if (at.m < 1 || at.m > 12) {
        at.m = 1;
    }
    if (at.wday < 0 || at.wday > 6) {
        at.wday = 0;
    }
    snprintf(out, cap, "%s %s %d", k_wd[at.wday], k_mon[at.m], at.d);
}

void zk_fmt_fire_when(const zk_kiosk_fire_t *fire, zk_wall_t now, char *out, size_t cap)
{
    char day[24];
    char t[16];
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    if (!fire || !fire->has_at) {
        return;
    }
    zk_fmt_relative_day(fire->at, now, day, sizeof day);
    zk_fmt_time12(fire->at.hh, fire->at.mm, t, sizeof t);
    snprintf(out, cap, "%s %s", day, t);
}

void zk_fmt_name_upper(const char *name, char *out, size_t cap)
{
    char tmp[ZK_NOTE_MAX];
    zk_str_trim_copy(tmp, sizeof tmp, name ? name : "");
    zk_str_upper(tmp);
    zk_str_copy(out, cap, tmp);
}

int zk_rain_strip_visible(const zk_kiosk_t *k)
{
    return k && k->rain_strip.show;
}

void zk_rain_strip_text(const zk_rain_strip_t *rs, char *out, size_t cap)
{
    char amt[16];
    int hours;
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    if (!rs) {
        return;
    }
    hours = rs->hours;
    if (hours != 24 && hours != 72) {
        hours = rs->hours > 0 ? rs->hours : 24;
    }
    zk_fmt_inches(rs->inches, amt, sizeof amt);
    snprintf(out, cap, "%s in fell in the last %d h", amt, hours);
}

int zk_station_soil_percent(const zk_kiosk_t *k, int station_index)
{
    int pct;
    if (!k || !k->soil.show_bars) {
        return -1;
    }
    if (station_index < 0 || station_index >= k->n_stations) {
        return -1;
    }
    pct = k->stations[station_index].soil_percent;
    if (pct < 0 || pct > 100) {
        return -1;
    }
    return pct;
}

int zk_soil_low(int pct)
{
    return pct >= 0 && pct < 40;
}

int zk_weekday_matches(unsigned mask, int wday)
{
    if (mask == 0) {
        return 1;
    }
    if (wday < 0 || wday > 6) {
        return 0;
    }
    return (mask & (1u << wday)) != 0;
}

int zk_in_season(const zk_schedule_t *sch, int y, int m, int d)
{
    if (!sch) {
        return 0;
    }
    if (sch->has_starts_on && zk_date_cmp(y, m, d, sch->starts_y, sch->starts_m, sch->starts_d) < 0) {
        return 0;
    }
    if (sch->has_ends_on && zk_date_cmp(y, m, d, sch->ends_y, sch->ends_m, sch->ends_d) > 0) {
        return 0;
    }
    return 1;
}

static void add_days(int y, int m, int d, int n, int *oy, int *om, int *od, int *owd)
{
    int z = zk_days_from_civil(y, m, d) + n;
    zk_civil_from_days(z, oy, om, od);
    if (owd) {
        zk_wall_t w;
        memset(&w, 0, sizeof(w));
        w.y = *oy;
        w.m = *om;
        w.d = *od;
        zk_wall_set_wday(&w);
        *owd = w.wday;
    }
}

zk_wall_t zk_morning_cutoff(const zk_schedules_t *schedules, int y, int m, int d)
{
    zk_wall_t w;
    int found = 0;
    int best_h = 0, best_mm = 0;
    int s;
    int wd;
    memset(&w, 0, sizeof(w));
    w.y = y;
    w.m = m;
    w.d = d;
    zk_wall_set_wday(&w);
    wd = w.wday;
    if (schedules) {
        for (s = 0; s < schedules->n; s++) {
            const zk_schedule_t *sch = &schedules->items[s];
            if (!sch->enabled || sch->n_steps <= 0 || !sch->has_start) {
                continue;
            }
            if (!zk_weekday_matches(sch->weekdays, wd) || !zk_in_season(sch, y, m, d)) {
                continue;
            }
            if (!found || sch->start_hh < best_h || (sch->start_hh == best_h && sch->start_mm < best_mm)) {
                found = 1;
                best_h = sch->start_hh;
                best_mm = sch->start_mm;
            }
        }
    }
    if (found && best_h < 12) {
        w.hh = best_h;
        w.mm = best_mm;
    } else {
        w.hh = 6;
        w.mm = 0;
    }
    w.ss = 0;
    return w;
}

int zk_pause_preview(const zk_schedules_t *schedules, zk_wall_t now,
                     zk_pause_kind_t kind, zk_pause_preview_t *out)
{
    int days = 0;
    int y, m, d, wd;
    zk_wall_t cut;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    switch (kind) {
    case ZK_PAUSE_TOMORROW:
        days = 1;
        zk_str_copy(out->primary, sizeof(out->primary), "Tomorrow morning");
        zk_str_copy(out->body, sizeof(out->body), "{\"until\":\"tomorrow_morning\",\"reason\":\"kiosk\"}");
        break;
    case ZK_PAUSE_DAYS2:
        days = 2;
        zk_str_copy(out->primary, sizeof(out->primary), "2 days");
        zk_str_copy(out->body, sizeof(out->body), "{\"days\":2,\"reason\":\"kiosk\"}");
        break;
    case ZK_PAUSE_WEEK:
        days = 7;
        zk_str_copy(out->primary, sizeof(out->primary), "1 week");
        zk_str_copy(out->body, sizeof(out->body), "{\"days\":7,\"reason\":\"kiosk\"}");
        break;
    case ZK_PAUSE_INDEFINITE:
    default:
        zk_str_copy(out->primary, sizeof(out->primary), "Until further notice");
        zk_str_copy(out->secondary, sizeof(out->secondary), "Until I resume");
        zk_str_copy(out->body, sizeof(out->body), "{\"indefinite\":true,\"reason\":\"kiosk\"}");
        return ZK_OK;
    }
    add_days(now.y, now.m, now.d, days, &y, &m, &d, &wd);
    cut = zk_morning_cutoff(schedules, y, m, d);
    if (kind == ZK_PAUSE_WEEK) {
        if (m < 1 || m > 12) {
            m = 1;
        }
        if (wd < 0 || wd > 6) {
            wd = 0;
        }
        snprintf(out->secondary, sizeof(out->secondary), "%s %s %d", k_wd[wd], k_mon[m], d);
    } else {
        char t[16];
        zk_fmt_time12(cut.hh, cut.mm, t, sizeof(t));
        if (wd < 0 || wd > 6) {
            wd = 0;
        }
        snprintf(out->secondary, sizeof(out->secondary), "%s %s", k_wd[wd], t);
    }
    return ZK_OK;
}

void zk_pause_title(const zk_pause_t *p, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    if (p && strcmp(p->source, "auto") == 0 && strcmp(p->reason, "rain") == 0) {
        zk_str_copy(out, cap, "PAUSED FOR RAIN");
        return;
    }
    zk_str_copy(out, cap, "PAUSED");
}

void zk_pause_until_text(const zk_pause_t *p, zk_wall_t now, char *out, size_t cap)
{
    zk_wall_t u;
    int64_t dt;
    char t[16];
    if (!out || cap == 0) {
        return;
    }
    if (!p || !p->has_until) {
        zk_str_copy(out, cap, "Until you resume");
        return;
    }
    u = p->until;
    dt = zk_wall_epoch_sec(u) - zk_wall_epoch_sec(now);
    zk_fmt_time12(u.hh, u.mm, t, sizeof(t));
    if (u.wday < 0 || u.wday > 6) {
        zk_wall_set_wday(&u);
    }
    if (u.m < 1 || u.m > 12) {
        u.m = 1;
    }
    if (dt <= 6 * 24 * 3600) {
        snprintf(out, cap, "Until %s %s", k_wd[u.wday], t);
    } else {
        snprintf(out, cap, "Until %s %s %d, %s", k_wd[u.wday], k_mon[u.m], u.d, t);
    }
}

const zk_kiosk_station_t *zk_kiosk_station(const zk_kiosk_t *k, const char *id)
{
    int i;
    if (!k || !id || !id[0]) {
        return NULL;
    }
    for (i = 0; i < k->n_stations; i++) {
        if (strcmp(k->stations[i].id, id) == 0) {
            return &k->stations[i];
        }
    }
    return NULL;
}

int zk_kiosk_station_index(const zk_kiosk_t *k, const char *id)
{
    int i;
    if (!k || !id || !id[0]) {
        return -1;
    }
    for (i = 0; i < k->n_stations; i++) {
        if (strcmp(k->stations[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
