#include "zk_model.h"

#include "cJSON.h"

#include <ctype.h>
#include <string.h>

void zk_str_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void zk_str_upper(char *s)
{
    if (!s) {
        return;
    }
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 32);
        }
    }
}

void zk_str_trim_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') {
        src++;
    }
    size_t n = strlen(src);
    while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t' || src[n - 1] == '\n' || src[n - 1] == '\r')) {
        n--;
    }
    if (n + 1 > cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

/* Howard Hinnant days_from_civil / civil_from_days (public domain). */
int zk_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int)doe - 719468;
}

void zk_civil_from_days(int z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y0 = (int)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d0 = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m0 = mp < 10u ? mp + 3u : mp - 9u;
    if (y) {
        *y = y0 + (m0 <= 2);
    }
    if (m) {
        *m = (int)m0;
    }
    if (d) {
        *d = (int)d0;
    }
}

static int weekday_from_days(int z)
{
    if (z >= -4) {
        return (z + 4) % 7;
    }
    return (z + 5) % 7 + 6;
}

void zk_wall_set_wday(zk_wall_t *w)
{
    if (!w) {
        return;
    }
    w->wday = weekday_from_days(zk_days_from_civil(w->y, w->m, w->d));
}

int zk_date_cmp(int y1, int m1, int d1, int y2, int m2, int d2)
{
    if (y1 != y2) {
        return y1 < y2 ? -1 : 1;
    }
    if (m1 != m2) {
        return m1 < m2 ? -1 : 1;
    }
    if (d1 != d2) {
        return d1 < d2 ? -1 : 1;
    }
    return 0;
}

int64_t zk_wall_epoch_sec(zk_wall_t w)
{
    int64_t days = zk_days_from_civil(w.y, w.m, w.d);
    return days * 86400 + (int64_t)w.hh * 3600 + (int64_t)w.mm * 60 + w.ss;
}

void zk_wall_add_seconds(zk_wall_t *w, int64_t sec)
{
    if (!w) {
        return;
    }
    int64_t sod = (int64_t)w->hh * 3600 + (int64_t)w->mm * 60 + w->ss + sec;
    int z = zk_days_from_civil(w->y, w->m, w->d);
    while (sod < 0) {
        sod += 86400;
        z--;
    }
    z += (int)(sod / 86400);
    sod %= 86400;
    zk_civil_from_days(z, &w->y, &w->m, &w->d);
    w->hh = (int)(sod / 3600);
    w->mm = (int)((sod % 3600) / 60);
    w->ss = (int)(sod % 60);
    zk_wall_set_wday(w);
}

zk_wall_t zk_wall_advance(zk_wall_t status_now, int64_t mono_now_ms, int64_t mono_at_poll_ms)
{
    int64_t dms = mono_now_ms - mono_at_poll_ms;
    zk_wall_add_seconds(&status_now, dms / 1000);
    return status_now;
}

static int take_n_digits(const char **p, int n, int *out)
{
    int v = 0;
    int i;
    if (!p || !*p) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        char c = (*p)[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = v * 10 + (c - '0');
    }
    *p += n;
    *out = v;
    return 0;
}

static int take_1to2_digits(const char **p, int *out)
{
    int v = 0;
    int n = 0;
    if (!p || !*p || **p < '0' || **p > '9') {
        return -1;
    }
    while (n < 2 && **p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        (*p)++;
        n++;
    }
    *out = v;
    return 0;
}

int zk_parse_ymd(const char *s, int *y, int *m, int *d)
{
    const char *p;
    int yy, mm, dd;
    if (!s || !y || !m || !d) {
        return ZK_ERR_ARG;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    p = s;
    if (take_n_digits(&p, 4, &yy) != 0 || *p != '-') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &mm) != 0 || *p != '-') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &dd) != 0) {
        return ZK_ERR_PARSE;
    }
    if (mm < 1 || mm > 12 || dd < 1 || dd > 31) {
        return ZK_ERR_PARSE;
    }
    *y = yy;
    *m = mm;
    *d = dd;
    return ZK_OK;
}

int zk_parse_wall(const char *rfc3339, zk_wall_t *out)
{
    const char *p;
    int y, m, d, hh, mm, ss;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!rfc3339 || !rfc3339[0]) {
        return ZK_ERR_PARSE;
    }
    p = rfc3339;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (take_n_digits(&p, 4, &y) != 0 || *p != '-') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &m) != 0 || *p != '-') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &d) != 0) {
        return ZK_ERR_PARSE;
    }
    if (*p != 'T' && *p != 't' && *p != ' ') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &hh) != 0 || *p != ':') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &mm) != 0 || *p != ':') {
        return ZK_ERR_PARSE;
    }
    p++;
    if (take_n_digits(&p, 2, &ss) != 0) {
        return ZK_ERR_PARSE;
    }
    if (m < 1 || m > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 60) {
        return ZK_ERR_PARSE;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
    }
    /* Offset is ignored: wall fields are the daemon's local clock. */
    out->y = y;
    out->m = m;
    out->d = d;
    out->hh = hh;
    out->mm = mm;
    out->ss = ss;
    zk_wall_set_wday(out);
    return ZK_OK;
}

static const cJSON *jobj(const cJSON *o, const char *k)
{
    if (!o || !k) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(o, k);
}

static int jbool(const cJSON *o, const char *k, int def)
{
    const cJSON *v = jobj(o, k);
    if (!v || cJSON_IsNull(v)) {
        return def;
    }
    if (cJSON_IsTrue(v)) {
        return 1;
    }
    if (cJSON_IsFalse(v)) {
        return 0;
    }
    return def;
}

static void jstr(const cJSON *o, const char *k, char *dst, size_t cap)
{
    const cJSON *v = jobj(o, k);
    if (!v || !cJSON_IsString(v) || !v->valuestring) {
        if (dst && cap) {
            dst[0] = 0;
        }
        return;
    }
    zk_str_copy(dst, cap, v->valuestring);
}

static int jnum(const cJSON *o, const char *k, double *out)
{
    const cJSON *v = jobj(o, k);
    if (!v || !cJSON_IsNumber(v)) {
        return 0;
    }
    *out = v->valuedouble;
    return 1;
}

static cJSON *parse_root(const char *json)
{
    if (!json || !json[0]) {
        return NULL;
    }
    return cJSON_Parse(json);
}

/* NaN, ±inf, or more than 1000 inches is not a usable rain total. */
static int rain_inches_ok(double v)
{
    if (v != v) {
        return 0;
    }
    if (v != 0.0 && v + v == v) {
        return 0;
    }
    if (v > 1000.0 || v < 0.0) {
        return 0;
    }
    return 1;
}

static int finite_nonneg(double v)
{
    if (v != v) {
        return 0;
    }
    if (v != 0.0 && v + v == v) {
        return 0;
    }
    if (v < 0.0) {
        return 0;
    }
    return 1;
}

static int clamp_sec(double v)
{
    if (!finite_nonneg(v)) {
        return 0;
    }
    if (v > 7.0 * 24.0 * 3600.0) {
        v = 7.0 * 24.0 * 3600.0;
    }
    return (int)v;
}

static int present_wrong(const cJSON *o, const char *k, int (*ok)(const cJSON *))
{
    const cJSON *v = jobj(o, k);
    if (!v || cJSON_IsNull(v)) {
        return 0;
    }
    return !ok(v);
}

static int is_string(const cJSON *v)
{
    return cJSON_IsString(v) && v->valuestring != NULL;
}

static int is_bool(const cJSON *v)
{
    return cJSON_IsBool(v);
}

static int is_number(const cJSON *v)
{
    return cJSON_IsNumber(v);
}

static int is_object(const cJSON *v)
{
    return cJSON_IsObject(v);
}

static int is_array(const cJSON *v)
{
    return cJSON_IsArray(v);
}

static int is_object_or_null(const cJSON *v)
{
    return cJSON_IsNull(v) || cJSON_IsObject(v);
}

static int is_array_or_null(const cJSON *v)
{
    return cJSON_IsNull(v) || cJSON_IsArray(v);
}

static int is_string_or_null(const cJSON *v)
{
    return cJSON_IsNull(v) || is_string(v);
}

static int is_number_or_null(const cJSON *v)
{
    return cJSON_IsNull(v) || cJSON_IsNumber(v);
}

static int parse_optional_wall(const cJSON *o, const char *k, zk_wall_t *out, int *has)
{
    const cJSON *v = jobj(o, k);
    if (has) {
        *has = 0;
    }
    if (!v || cJSON_IsNull(v)) {
        return 0;
    }
    if (!is_string(v) || !v->valuestring[0]) {
        return -1;
    }
    if (zk_parse_wall(v->valuestring, out) != ZK_OK) {
        return -1;
    }
    if (has) {
        *has = 1;
    }
    return 0;
}

static int parse_fire(const cJSON *o, zk_kiosk_fire_t *out, int *has)
{
    double mins;
    memset(out, 0, sizeof *out);
    if (has) {
        *has = 0;
    }
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "schedule_id", is_string) || present_wrong(o, "name", is_string) ||
        present_wrong(o, "at", is_string) || present_wrong(o, "ends_at", is_string) ||
        present_wrong(o, "total_min", is_number) || present_wrong(o, "skipped_by_pause", is_bool)) {
        return -1;
    }
    jstr(o, "schedule_id", out->schedule_id, sizeof out->schedule_id);
    jstr(o, "name", out->name, sizeof out->name);
    if (parse_optional_wall(o, "at", &out->at, &out->has_at) != 0) {
        return -1;
    }
    if (parse_optional_wall(o, "ends_at", &out->ends_at, &out->has_ends_at) != 0) {
        return -1;
    }
    mins = 0;
    if (jnum(o, "total_min", &mins)) {
        if (!finite_nonneg(mins) || mins > 7.0 * 24.0 * 60.0) {
            mins = 0;
        }
        out->total_min = (int)mins;
    }
    out->skipped_by_pause = jbool(o, "skipped_by_pause", 0);
    if (has) {
        *has = 1;
    }
    return 0;
}

static int parse_run_steps(const cJSON *arr, zk_kiosk_run_t *run)
{
    int i, n;
    if (!arr || !cJSON_IsArray(arr)) {
        return 0;
    }
    n = cJSON_GetArraySize(arr);
    if (n < 0) {
        return -1;
    }
    for (i = 0; i < n && run->n_steps < ZK_MAX_STEPS; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        zk_kiosk_run_step_t *st;
        double v;
        if (!it || !cJSON_IsObject(it)) {
            continue;
        }
        if (present_wrong(it, "station_id", is_string) || present_wrong(it, "title", is_string) ||
            present_wrong(it, "planned_sec", is_number) || present_wrong(it, "elapsed_sec", is_number) ||
            present_wrong(it, "remaining_sec", is_number) || present_wrong(it, "state", is_string)) {
            return -1;
        }
        st = &run->steps[run->n_steps];
        memset(st, 0, sizeof *st);
        jstr(it, "station_id", st->station_id, sizeof st->station_id);
        jstr(it, "title", st->title, sizeof st->title);
        jstr(it, "state", st->state, sizeof st->state);
        v = 0;
        if (jnum(it, "planned_sec", &v)) {
            st->planned_sec = clamp_sec(v);
        }
        v = 0;
        if (jnum(it, "elapsed_sec", &v)) {
            st->elapsed_sec = clamp_sec(v);
        }
        v = 0;
        if (jnum(it, "remaining_sec", &v)) {
            st->remaining_sec = clamp_sec(v);
        }
        run->n_steps++;
    }
    return 0;
}

static int parse_run(const cJSON *o, zk_kiosk_run_t *out, int *has)
{
    const cJSON *steps;
    double v;
    memset(out, 0, sizeof *out);
    out->step_index = -1;
    if (has) {
        *has = 0;
    }
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "kind", is_string) || present_wrong(o, "program", is_string) ||
        present_wrong(o, "current_station", is_string) || present_wrong(o, "next_station", is_string) ||
        present_wrong(o, "step_index", is_number) || present_wrong(o, "step_count", is_number) ||
        present_wrong(o, "step_elapsed_sec", is_number) || present_wrong(o, "step_remaining_sec", is_number) ||
        present_wrong(o, "run_remaining_sec", is_number) || present_wrong(o, "run_total_sec", is_number) ||
        present_wrong(o, "steps", is_array)) {
        return -1;
    }
    jstr(o, "kind", out->kind, sizeof out->kind);
    jstr(o, "program", out->program, sizeof out->program);
    jstr(o, "current_station", out->current_station, sizeof out->current_station);
    jstr(o, "next_station", out->next_station, sizeof out->next_station);
    v = -1;
    if (jnum(o, "step_index", &v)) {
        if (v != v || (v != 0.0 && v + v == v) || v < 0) {
            out->step_index = -1;
        } else if (v > ZK_MAX_STEPS) {
            out->step_index = ZK_MAX_STEPS;
        } else {
            out->step_index = (int)v;
        }
    } else {
        out->step_index = -1;
    }
    v = 0;
    if (jnum(o, "step_count", &v)) {
        out->step_count = clamp_sec(v);
        if (out->step_count > ZK_MAX_STEPS) {
            out->step_count = ZK_MAX_STEPS;
        }
    }
    v = 0;
    if (jnum(o, "step_elapsed_sec", &v)) {
        out->step_elapsed_sec = clamp_sec(v);
    }
    v = 0;
    if (jnum(o, "step_remaining_sec", &v)) {
        out->step_remaining_sec = clamp_sec(v);
    }
    v = 0;
    if (jnum(o, "run_remaining_sec", &v)) {
        out->run_remaining_sec = clamp_sec(v);
    }
    v = 0;
    if (jnum(o, "run_total_sec", &v)) {
        out->run_total_sec = clamp_sec(v);
    }
    steps = jobj(o, "steps");
    if (parse_run_steps(steps, out) != 0) {
        return -1;
    }
    if (has) {
        *has = 1;
    }
    return 0;
}

static int parse_pause(const cJSON *o, zk_pause_t *out)
{
    double inches;
    memset(out, 0, sizeof *out);
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "paused", is_bool) || present_wrong(o, "until", is_string_or_null) ||
        present_wrong(o, "label", is_string) || present_wrong(o, "reason", is_string) ||
        present_wrong(o, "source", is_string) || present_wrong(o, "rain_inches", is_number)) {
        return -1;
    }
    out->paused = jbool(o, "paused", 0);
    jstr(o, "label", out->label, sizeof out->label);
    jstr(o, "reason", out->reason, sizeof out->reason);
    jstr(o, "source", out->source, sizeof out->source);
    if (parse_optional_wall(o, "until", &out->until, &out->has_until) != 0) {
        return -1;
    }
    inches = 0;
    if (jnum(o, "rain_inches", &inches)) {
        out->rain_inches = rain_inches_ok(inches) ? inches : 0;
    }
    return 0;
}

static int parse_rain_strip(const cJSON *o, zk_rain_strip_t *out)
{
    double inches, hours;
    memset(out, 0, sizeof *out);
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "show", is_bool) || present_wrong(o, "inches", is_number) ||
        present_wrong(o, "hours", is_number)) {
        return -1;
    }
    out->show = jbool(o, "show", 0);
    inches = 0;
    if (jnum(o, "inches", &inches)) {
        out->inches = rain_inches_ok(inches) ? inches : 0;
    }
    hours = 0;
    if (jnum(o, "hours", &hours)) {
        if (!finite_nonneg(hours) || hours > 72) {
            out->hours = 0;
        } else {
            out->hours = (int)hours;
        }
    }
    return 0;
}

static int parse_rain(const cJSON *o, zk_rain_t *out)
{
    memset(out, 0, sizeof *out);
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "enabled", is_bool) || present_wrong(o, "unavailable", is_bool) ||
        present_wrong(o, "have_totals", is_bool) || present_wrong(o, "total_24h_inches", is_number) ||
        present_wrong(o, "total_72h_inches", is_number)) {
        return -1;
    }
    out->enabled = jbool(o, "enabled", 0);
    out->unavailable = jbool(o, "unavailable", 0);
    out->have_totals = jbool(o, "have_totals", 0);
    if (!jnum(o, "total_24h_inches", &out->total_24h)) {
        out->total_24h = 0;
    }
    if (!jnum(o, "total_72h_inches", &out->total_72h)) {
        out->total_72h = 0;
    }
    if (out->have_totals && (!rain_inches_ok(out->total_24h) || !rain_inches_ok(out->total_72h))) {
        out->have_totals = 0;
        out->total_24h = 0;
        out->total_72h = 0;
    }
    return 0;
}

static int parse_soil_obj(const cJSON *o, zk_kiosk_soil_t *out)
{
    memset(out, 0, sizeof *out);
    if (!o || cJSON_IsNull(o)) {
        return 0;
    }
    if (!cJSON_IsObject(o)) {
        return -1;
    }
    if (present_wrong(o, "enabled", is_bool) || present_wrong(o, "et_known", is_bool) ||
        present_wrong(o, "et_stale", is_bool) || present_wrong(o, "show_bars", is_bool) ||
        present_wrong(o, "updated_at", is_string_or_null)) {
        return -1;
    }
    out->enabled = jbool(o, "enabled", 0);
    out->et_known = jbool(o, "et_known", 0);
    out->et_stale = jbool(o, "et_stale", 0);
    out->show_bars = jbool(o, "show_bars", 0);
    if (parse_optional_wall(o, "updated_at", &out->updated_at, &out->has_updated_at) != 0) {
        return -1;
    }
    return 0;
}

static int parse_soil_percent(const cJSON *it, int *pct)
{
    const cJSON *v;
    double n;
    *pct = -1;
    v = jobj(it, "soil_percent");
    if (!v || cJSON_IsNull(v)) {
        return 0;
    }
    if (!cJSON_IsNumber(v)) {
        return -1;
    }
    n = v->valuedouble;
    if (!finite_nonneg(n) || n > 100.0) {
        *pct = -1;
        return 0;
    }
    *pct = (int)(n + 0.5);
    if (*pct < 0 || *pct > 100) {
        *pct = -1;
    }
    return 0;
}

static int parse_stations_arr(const cJSON *arr, zk_kiosk_t *out)
{
    int i, n;
    if (!arr || cJSON_IsNull(arr)) {
        return 0;
    }
    if (!cJSON_IsArray(arr)) {
        return -1;
    }
    n = cJSON_GetArraySize(arr);
    if (n < 0) {
        return -1;
    }
    for (i = 0; i < n && out->n_stations < ZK_MAX_STATIONS; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        zk_kiosk_station_t *st;
        if (!it || !cJSON_IsObject(it)) {
            continue;
        }
        if (present_wrong(it, "id", is_string) || present_wrong(it, "title", is_string) ||
            present_wrong(it, "color", is_string) || present_wrong(it, "on", is_bool) ||
            present_wrong(it, "state", is_string) || present_wrong(it, "rain_pause_exempt", is_bool) ||
            present_wrong(it, "soil_percent", is_number_or_null)) {
            return -1;
        }
        st = &out->stations[out->n_stations];
        memset(st, 0, sizeof *st);
        st->soil_percent = -1;
        jstr(it, "id", st->id, sizeof st->id);
        jstr(it, "title", st->title, sizeof st->title);
        jstr(it, "color", st->color, sizeof st->color);
        jstr(it, "state", st->state, sizeof st->state);
        st->on = jbool(it, "on", 0);
        st->rain_pause_exempt = jbool(it, "rain_pause_exempt", 0);
        if (parse_soil_percent(it, &st->soil_percent) != 0) {
            return -1;
        }
        if (!st->id[0] && !st->title[0]) {
            continue;
        }
        out->n_stations++;
    }
    return 0;
}

static int parse_stations_on(const cJSON *arr, zk_kiosk_t *out)
{
    int i, n;
    if (!arr || cJSON_IsNull(arr)) {
        return 0;
    }
    if (!cJSON_IsArray(arr)) {
        return -1;
    }
    n = cJSON_GetArraySize(arr);
    for (i = 0; i < n && out->n_on < ZK_MAX_ON; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (it && cJSON_IsString(it) && it->valuestring && it->valuestring[0]) {
            zk_str_copy(out->stations_on[out->n_on], ZK_ID_MAX, it->valuestring);
            out->n_on++;
        }
    }
    return 0;
}

int zk_parse_kiosk(const char *json, zk_kiosk_t *out)
{
    cJSON *root;
    const cJSON *pause;
    const cJSON *strip;
    const cJSON *rain;
    const cJSON *soil;
    const cJSON *stations;
    const cJSON *on;
    const cJSON *next;
    const cJSON *eff;
    const cJSON *run;
    int rc = ZK_ERR_PARSE;

    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof *out);
    out->run.step_index = -1;
    {
        int i;
        for (i = 0; i < ZK_MAX_STATIONS; i++) {
            out->stations[i].soil_percent = -1;
        }
    }
    root = parse_root(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    if (present_wrong(root, "now", is_string) || present_wrong(root, "timezone", is_string) ||
        present_wrong(root, "phase", is_string) || present_wrong(root, "lockout", is_bool) ||
        present_wrong(root, "last_error", is_string) || present_wrong(root, "current_station", is_string) ||
        present_wrong(root, "stations_on", is_array_or_null) || present_wrong(root, "pause", is_object) ||
        present_wrong(root, "rain_strip", is_object) || present_wrong(root, "rain", is_object) ||
        present_wrong(root, "next_run", is_object_or_null) ||
        present_wrong(root, "next_effective_run", is_object_or_null) ||
        present_wrong(root, "run", is_object_or_null) || present_wrong(root, "stations", is_array) ||
        present_wrong(root, "soil", is_object)) {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }

    jstr(root, "phase", out->phase, sizeof out->phase);
    out->fault = (strcmp(out->phase, "Fault") == 0);
    out->watering = (out->phase[0] && strcmp(out->phase, "Idle") != 0 && strcmp(out->phase, "Fault") != 0);
    jstr(root, "timezone", out->timezone, sizeof out->timezone);
    jstr(root, "last_error", out->last_error, sizeof out->last_error);
    jstr(root, "current_station", out->current_station, sizeof out->current_station);
    out->lockout = jbool(root, "lockout", 0);
    if (parse_optional_wall(root, "now", &out->now, &out->has_now) != 0) {
        goto done;
    }

    on = jobj(root, "stations_on");
    if (parse_stations_on(on, out) != 0) {
        goto done;
    }
    pause = jobj(root, "pause");
    if (parse_pause(pause, &out->pause) != 0) {
        goto done;
    }
    strip = jobj(root, "rain_strip");
    if (parse_rain_strip(strip, &out->rain_strip) != 0) {
        goto done;
    }
    rain = jobj(root, "rain");
    if (parse_rain(rain, &out->rain) != 0) {
        goto done;
    }
    next = jobj(root, "next_run");
    if (parse_fire(next, &out->next_run, &out->has_next_run) != 0) {
        goto done;
    }
    eff = jobj(root, "next_effective_run");
    if (parse_fire(eff, &out->next_effective, &out->has_next_effective) != 0) {
        goto done;
    }
    run = jobj(root, "run");
    if (parse_run(run, &out->run, &out->has_run) != 0) {
        goto done;
    }
    stations = jobj(root, "stations");
    if (parse_stations_arr(stations, out) != 0) {
        goto done;
    }
    soil = jobj(root, "soil");
    if (parse_soil_obj(soil, &out->soil) != 0) {
        goto done;
    }
    if (out->has_run) {
        out->watering = 1;
    }
    rc = ZK_OK;
done:
    cJSON_Delete(root);
    if (rc != ZK_OK) {
        memset(out, 0, sizeof *out);
        out->run.step_index = -1;
    }
    return rc;
}

static int parse_wd_token(const char *s)
{
    char a, b, c;
    while (s && (*s == ' ' || *s == '\t')) {
        s++;
    }
    if (!s || !s[0] || !s[1] || !s[2]) {
        return -1;
    }
    a = (char)tolower((unsigned char)s[0]);
    b = (char)tolower((unsigned char)s[1]);
    c = (char)tolower((unsigned char)s[2]);
    if (a == 's' && b == 'u' && c == 'n') {
        return 0;
    }
    if (a == 'm' && b == 'o' && c == 'n') {
        return 1;
    }
    if (a == 't' && b == 'u' && c == 'e') {
        return 2;
    }
    if (a == 'w' && b == 'e' && c == 'd') {
        return 3;
    }
    if (a == 't' && b == 'h' && c == 'u') {
        return 4;
    }
    if (a == 'f' && b == 'r' && c == 'i') {
        return 5;
    }
    if (a == 's' && b == 'a' && c == 't') {
        return 6;
    }
    return -1;
}

static int parse_hhmm(const char *s, int *hh, int *mm)
{
    const char *p;
    int h, m;
    if (!s) {
        return -1;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    p = s;
    if (take_1to2_digits(&p, &h) != 0 || *p != ':') {
        return -1;
    }
    p++;
    if (take_n_digits(&p, 2, &m) != 0) {
        return -1;
    }
    if (h > 23 || m > 59) {
        return -1;
    }
    *hh = h;
    *mm = m;
    return 0;
}

static void parse_one_schedule(const cJSON *it, zk_schedule_t *sch)
{
    const cJSON *wds;
    const cJSON *steps;
    const cJSON *start;
    char tmp[32];
    int i, n, wd;
    memset(sch, 0, sizeof(*sch));
    jstr(it, "id", sch->id, sizeof(sch->id));
    jstr(it, "note", sch->note, sizeof(sch->note));
    sch->enabled = jbool(it, "enabled", 0);
    start = jobj(it, "start");
    if (start && cJSON_IsString(start) && start->valuestring) {
        if (parse_hhmm(start->valuestring, &sch->start_hh, &sch->start_mm) == 0) {
            sch->has_start = 1;
        }
    }
    wds = jobj(it, "weekdays");
    if (wds && cJSON_IsArray(wds)) {
        n = cJSON_GetArraySize(wds);
        for (i = 0; i < n; i++) {
            const cJSON *w = cJSON_GetArrayItem(wds, i);
            if (!w || !cJSON_IsString(w) || !w->valuestring) {
                continue;
            }
            wd = parse_wd_token(w->valuestring);
            if (wd >= 0) {
                sch->weekdays |= (1u << wd);
            }
        }
    }
    steps = jobj(it, "steps");
    if (steps && cJSON_IsArray(steps)) {
        n = cJSON_GetArraySize(steps);
        for (i = 0; i < n && sch->n_steps < ZK_MAX_STEPS; i++) {
            const cJSON *st = cJSON_GetArrayItem(steps, i);
            double mins;
            if (!st || !cJSON_IsObject(st)) {
                continue;
            }
            jstr(st, "station_id", sch->steps[sch->n_steps].station_id, ZK_ID_MAX);
            mins = 0;
            if (jnum(st, "minutes", &mins)) {
                if (mins < 0) {
                    mins = 0;
                }
                if (mins > 24 * 60) {
                    mins = 24 * 60;
                }
                sch->steps[sch->n_steps].minutes = (int)mins;
            }
            sch->n_steps++;
        }
    }
    tmp[0] = 0;
    jstr(it, "starts_on", tmp, sizeof(tmp));
    if (tmp[0] && zk_parse_ymd(tmp, &sch->starts_y, &sch->starts_m, &sch->starts_d) == ZK_OK) {
        sch->has_starts_on = 1;
    }
    tmp[0] = 0;
    jstr(it, "ends_on", tmp, sizeof(tmp));
    if (tmp[0] && zk_parse_ymd(tmp, &sch->ends_y, &sch->ends_m, &sch->ends_d) == ZK_OK) {
        sch->has_ends_on = 1;
    }
}

int zk_parse_schedules(const char *json, zk_schedules_t *out)
{
    cJSON *root;
    const cJSON *arr;
    int i, n;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    root = parse_root(json);
    if (!root) {
        return ZK_ERR_PARSE;
    }
    if (cJSON_IsArray(root)) {
        arr = root;
    } else if (cJSON_IsObject(root)) {
        arr = jobj(root, "schedules");
        if (!arr || !cJSON_IsArray(arr)) {
            cJSON_Delete(root);
            return ZK_OK;
        }
    } else {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    n = cJSON_GetArraySize(arr);
    for (i = 0; i < n && out->n < ZK_MAX_SCHEDULES; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        if (!it || !cJSON_IsObject(it)) {
            continue;
        }
        parse_one_schedule(it, &out->items[out->n]);
        if (!out->items[out->n].id[0] && !out->items[out->n].note[0]) {
            continue;
        }
        out->n++;
    }
    cJSON_Delete(root);
    return ZK_OK;
}

