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
        /* +inf and anything the strip cannot show. */
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

int zk_rain_strip_visible(const zk_status_t *st)
{
    if (!st) {
        return 0;
    }
    if (!st->rain.enabled || st->rain.unavailable || !st->rain.have_totals) {
        return 0;
    }
    return (st->rain.total_72h + 1e-9) >= 0.05;
}

void zk_rain_strip_text(const zk_status_t *st, char *out, size_t cap)
{
    double inches;
    int hours;
    char amt[16];
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    if (!st || !st->rain.have_totals) {
        return;
    }
    if ((st->rain.total_24h + 1e-9) >= 0.05) {
        hours = 24;
        inches = st->rain.total_24h;
    } else {
        hours = 72;
        inches = st->rain.total_72h;
    }
    zk_fmt_inches(inches, amt, sizeof(amt));
    snprintf(out, cap, "%s in fell in the last %d hours", amt, hours);
}

int zk_soil_percent(const zk_soil_t *soil, const char *station_id)
{
    int i;
    if (!soil || !station_id || !soil->enabled || !soil->et_known || soil->et_stale) {
        return -1;
    }
    for (i = 0; i < soil->n_zones; i++) {
        if (strcmp(soil->zones[i].station_id, station_id) != 0) {
            continue;
        }
        if (!soil->zones[i].rate_measured || soil->zones[i].percent < 0) {
            return -1;
        }
        return soil->zones[i].percent;
    }
    return -1;
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

static void sched_name_upper(const zk_schedule_t *sch, char *out, size_t cap)
{
    char tmp[ZK_NOTE_MAX];
    zk_str_trim_copy(tmp, sizeof(tmp), sch->note);
    if (!tmp[0]) {
        zk_str_trim_copy(tmp, sizeof(tmp), sch->id);
    }
    zk_str_upper(tmp);
    zk_str_copy(out, cap, tmp);
}

static void sched_summary(const zk_schedule_t *sch, char *out, size_t cap)
{
    char t[16];
    unsigned m;
    const int order[7] = {1, 2, 3, 4, 5, 6, 0};
    char days[48];
    int n = 0;
    int i;
    size_t pos = 0;
    zk_fmt_time12(sch->start_hh, sch->start_mm, t, sizeof(t));
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
        int wd = order[i];
        if (m & (1u << wd)) {
            int wr;
            if (n) {
                wr = snprintf(days + pos, sizeof(days) - pos, " %s", k_wd[wd]);
            } else {
                wr = snprintf(days + pos, sizeof(days) - pos, "%s", k_wd[wd]);
            }
            if (wr < 0) {
                break;
            }
            pos += (size_t)wr;
            if (pos >= sizeof(days)) {
                pos = sizeof(days) - 1;
                break;
            }
            n++;
        }
    }
    snprintf(out, cap, "%s %s", days, t);
}

static void fmt_day_word(int i, int y, int m, int d, int wday, char *out, size_t cap)
{
    if (i <= 0) {
        zk_str_copy(out, cap, "Today");
        return;
    }
    if (i == 1) {
        zk_str_copy(out, cap, "Tomorrow");
        return;
    }
    if (wday < 0 || wday > 6) {
        wday = 0;
    }
    if (i <= 6) {
        zk_str_copy(out, cap, k_wd[wday]);
        return;
    }
    if (m < 1 || m > 12) {
        m = 1;
    }
    snprintf(out, cap, "%s %s %d", k_wd[wday], k_mon[m], d);
    (void)y;
}

int zk_next_run(const zk_schedules_t *schedules, const zk_stations_t *stations,
                zk_wall_t now, zk_next_run_t *out)
{
    int best_i = 9999;
    int best_hm = 9999;
    int best_idx = -1;
    int now_hm;
    int i, s;
    (void)stations;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->sched_index = -1;
    if (!schedules) {
        return ZK_OK;
    }
    now_hm = now.hh * 60 + now.mm;
    for (i = 0; i < 400; i++) {
        int y, m, d, wd;
        add_days(now.y, now.m, now.d, i, &y, &m, &d, &wd);
        for (s = 0; s < schedules->n; s++) {
            const zk_schedule_t *sch = &schedules->items[s];
            int hm;
            if (!sch->enabled || !sch->has_start) {
                continue;
            }
            if (!zk_weekday_matches(sch->weekdays, wd)) {
                continue;
            }
            if (!zk_in_season(sch, y, m, d)) {
                continue;
            }
            hm = sch->start_hh * 60 + sch->start_mm;
            if (i == 0 && hm <= now_hm) {
                continue;
            }
            if (best_idx < 0 || i < best_i || (i == best_i && hm < best_hm)) {
                best_i = i;
                best_hm = hm;
                best_idx = s;
            }
        }
        if (best_idx >= 0 && i == best_i) {
            /* earliest day is locked; later days cannot beat it */
            break;
        }
    }
    if (best_idx < 0) {
        return ZK_OK;
    }
    {
        const zk_schedule_t *sch = &schedules->items[best_idx];
        int y, m, d, wd;
        add_days(now.y, now.m, now.d, best_i, &y, &m, &d, &wd);
        out->have = 1;
        out->sched_index = best_idx;
        fmt_day_word(best_i, y, m, d, wd, out->day, sizeof(out->day));
        zk_fmt_time12(sch->start_hh, sch->start_mm, out->time, sizeof(out->time));
        sched_name_upper(sch, out->name, sizeof(out->name));
        sched_summary(sch, out->summary, sizeof(out->summary));
    }
    return ZK_OK;
}

int zk_run_infer(const zk_status_t *status, const zk_schedules_t *schedules,
                 zk_wall_t now, zk_run_info_t *info)
{
    const char *cur;
    int now_sec;
    int s;
    if (!info) {
        return ZK_ERR_ARG;
    }
    memset(info, 0, sizeof(*info));
    info->remaining_sec = -1;
    if (!status || !status->watering) {
        return ZK_OK;
    }
    cur = status->current_station;
    if (!cur[0] && status->n_on > 0) {
        cur = status->stations_on[0];
    }
    if (!cur || !cur[0]) {
        return ZK_OK;
    }
    info->have = 1;
    zk_str_copy(info->current_title_id, sizeof(info->current_title_id), cur);
    info->step_count = 1;
    info->remaining_sec = -1;
    if (!schedules) {
        return ZK_OK;
    }
    now_sec = now.hh * 3600 + now.mm * 60 + now.ss;
    for (s = 0; s < schedules->n; s++) {
        const zk_schedule_t *sch = &schedules->items[s];
        int start_sec, elapsed, total_min, t, i, idx;
        if (!sch->enabled || !sch->has_start || sch->n_steps <= 0) {
            continue;
        }
        if (!zk_weekday_matches(sch->weekdays, now.wday)) {
            continue;
        }
        if (!zk_in_season(sch, now.y, now.m, now.d)) {
            continue;
        }
        start_sec = sch->start_hh * 3600 + sch->start_mm * 60;
        elapsed = now_sec - start_sec;
        if (elapsed < -60) {
            continue;
        }
        if (elapsed < 0) {
            elapsed = 0;
        }
        total_min = 0;
        for (i = 0; i < sch->n_steps; i++) {
            total_min += sch->steps[i].minutes;
        }
        if (elapsed > total_min * 60 + 90) {
            continue;
        }
        t = 0;
        for (i = 0; i < sch->n_steps; i++) {
            int mins = sch->steps[i].minutes;
            int end = t + mins * 60;
            if (strcmp(sch->steps[i].station_id, cur) == 0 && elapsed <= end + 30) {
                info->step_index = i;
                info->step_count = sch->n_steps;
                info->remaining_sec = end - elapsed;
                if (info->remaining_sec < 0) {
                    info->remaining_sec = 0;
                }
                info->total_step_sec = mins * 60;
                if (i + 1 < sch->n_steps) {
                    zk_str_copy(info->next_station_id, sizeof(info->next_station_id),
                                sch->steps[i + 1].station_id);
                } else {
                    info->next_station_id[0] = 0;
                }
                return ZK_OK;
            }
            t = end;
        }
        idx = -1;
        for (i = 0; i < sch->n_steps; i++) {
            if (strcmp(sch->steps[i].station_id, cur) == 0) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            int mins = sch->steps[idx].minutes;
            info->step_index = idx;
            info->step_count = sch->n_steps;
            info->remaining_sec = mins * 60;
            info->total_step_sec = mins * 60;
            if (idx + 1 < sch->n_steps) {
                zk_str_copy(info->next_station_id, sizeof(info->next_station_id),
                            sch->steps[idx + 1].station_id);
            }
            return ZK_OK;
        }
    }
    return ZK_OK;
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

void zk_pause_title(const zk_status_t *st, char *out, size_t cap)
{
    if (!out || cap == 0) {
        return;
    }
    if (st && strcmp(st->pause_source, "auto") == 0 && strcmp(st->reason, "rain") == 0) {
        zk_str_copy(out, cap, "PAUSED FOR RAIN");
        return;
    }
    zk_str_copy(out, cap, "PAUSED");
}

void zk_pause_until_text(const zk_status_t *st, zk_wall_t now, char *out, size_t cap)
{
    zk_wall_t u;
    int64_t dt;
    char t[16];
    if (!out || cap == 0) {
        return;
    }
    if (!st || !st->has_paused_until) {
        zk_str_copy(out, cap, "Until you resume");
        return;
    }
    u = st->paused_until;
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
