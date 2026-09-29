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

int zk_parse_status(const char *json, zk_status_t *out)
{
    cJSON *root;
    const cJSON *rain;
    const cJSON *on;
    const cJSON *pu;
    const cJSON *now;
    int i, n;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    root = parse_root(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    jstr(root, "phase", out->phase, sizeof(out->phase));
    out->fault = (strcmp(out->phase, "Fault") == 0);
    out->watering = (out->phase[0] && strcmp(out->phase, "Idle") != 0 && strcmp(out->phase, "Fault") != 0);
    jstr(root, "current_station", out->current_station, sizeof(out->current_station));
    jstr(root, "last_error", out->last_error, sizeof(out->last_error));
    out->lockout = jbool(root, "lockout", 0);
    out->paused = jbool(root, "paused", 0);
    jstr(root, "paused_label", out->paused_label, sizeof(out->paused_label));
    jstr(root, "pause_source", out->pause_source, sizeof(out->pause_source));
    jstr(root, "reason", out->reason, sizeof(out->reason));
    jstr(root, "timezone", out->timezone, sizeof(out->timezone));

    pu = jobj(root, "paused_until");
    if (pu && cJSON_IsString(pu) && pu->valuestring && pu->valuestring[0]) {
        if (zk_parse_wall(pu->valuestring, &out->paused_until) == ZK_OK) {
            out->has_paused_until = 1;
        }
    }
    now = jobj(root, "now");
    if (now && cJSON_IsString(now) && now->valuestring && now->valuestring[0]) {
        if (zk_parse_wall(now->valuestring, &out->now) == ZK_OK) {
            out->has_now = 1;
        }
    }

    on = jobj(root, "stations_on");
    if (on && cJSON_IsArray(on)) {
        n = cJSON_GetArraySize(on);
        for (i = 0; i < n && out->n_on < ZK_MAX_ON; i++) {
            const cJSON *it = cJSON_GetArrayItem(on, i);
            if (it && cJSON_IsString(it) && it->valuestring && it->valuestring[0]) {
                zk_str_copy(out->stations_on[out->n_on], ZK_ID_MAX, it->valuestring);
                out->n_on++;
            }
        }
    }

    rain = jobj(root, "rain");
    if (rain && cJSON_IsObject(rain)) {
        out->rain.enabled = jbool(rain, "enabled", 0);
        out->rain.unavailable = jbool(rain, "unavailable", 0);
        out->rain.have_totals = 0;
        out->rain.total_24h = 0;
        out->rain.total_72h = 0;
        if (jnum(rain, "total_24h_inches", &out->rain.total_24h)) {
            /* keep */
        } else {
            out->rain.total_24h = 0;
        }
        if (jnum(rain, "total_72h_inches", &out->rain.total_72h)) {
            out->rain.have_totals = 1;
        } else {
            out->rain.total_72h = 0;
        }
    }

    cJSON_Delete(root);
    return ZK_OK;
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

int zk_parse_stations(const char *json, zk_stations_t *out)
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
        arr = jobj(root, "stations");
        if (!arr || !cJSON_IsArray(arr)) {
            cJSON_Delete(root);
            return ZK_OK;
        }
    } else {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    n = cJSON_GetArraySize(arr);
    for (i = 0; i < n && out->n < ZK_MAX_STATIONS; i++) {
        const cJSON *it = cJSON_GetArrayItem(arr, i);
        zk_station_t *st;
        if (!it || !cJSON_IsObject(it)) {
            continue;
        }
        st = &out->items[out->n];
        jstr(it, "id", st->id, sizeof(st->id));
        jstr(it, "title", st->title, sizeof(st->title));
        jstr(it, "color", st->color, sizeof(st->color));
        if (!st->id[0] && !st->title[0]) {
            continue;
        }
        out->n++;
    }
    cJSON_Delete(root);
    return ZK_OK;
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

int zk_parse_soil(const char *json, zk_soil_t *out)
{
    cJSON *root;
    const cJSON *zones;
    int i, n;
    if (!out) {
        return ZK_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    root = parse_root(json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ZK_ERR_PARSE;
    }
    out->enabled = jbool(root, "enabled", 0);
    out->et_known = jbool(root, "et_known", 0);
    out->et_stale = jbool(root, "et_stale", 0);
    zones = jobj(root, "zones");
    if (zones && cJSON_IsArray(zones)) {
        n = cJSON_GetArraySize(zones);
        for (i = 0; i < n && out->n_zones < ZK_MAX_ZONES; i++) {
            const cJSON *z = cJSON_GetArrayItem(zones, i);
            const cJSON *pct;
            zk_zone_t *dst;
            if (!z || !cJSON_IsObject(z)) {
                continue;
            }
            dst = &out->zones[out->n_zones];
            jstr(z, "station_id", dst->station_id, ZK_ID_MAX);
            dst->percent = -1;
            dst->rate_measured = jbool(z, "rate_measured", 0);
            pct = jobj(z, "percent");
            if (pct && cJSON_IsNumber(pct)) {
                double v = pct->valuedouble;
                if (v < 0) {
                    v = 0;
                }
                if (v > 100) {
                    v = 100;
                }
                dst->percent = (int)(v + (v >= 0 ? 0.5 : -0.5));
                if (dst->percent < 0) {
                    dst->percent = 0;
                }
                if (dst->percent > 100) {
                    dst->percent = 100;
                }
            }
            if (!dst->station_id[0]) {
                continue;
            }
            out->n_zones++;
        }
    }
    cJSON_Delete(root);
    return ZK_OK;
}
