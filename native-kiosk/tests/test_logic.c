#include "zk_test.h"
#include "zk_logic.h"

static zk_wall_t wall(int y, int m, int d, int hh, int mm, int ss)
{
    zk_wall_t w;
    memset(&w, 0, sizeof(w));
    w.y = y;
    w.m = m;
    w.d = d;
    w.hh = hh;
    w.mm = mm;
    w.ss = ss;
    zk_wall_set_wday(&w);
    return w;
}

static void load_home(zk_kiosk_t *k, zk_schedules_t *sch)
{
    char *a = zk_fixture("home-rain", "kiosk.json");
    char *c = zk_fixture("home-rain", "schedules.json");
    TCHECK(a && c, "home-rain fixtures");
    TEQ_I(zk_parse_kiosk(a, k), ZK_OK);
    TEQ_I(zk_parse_schedules(c, sch), ZK_OK);
    free(a);
    free(c);
}

static void test_fmt_fire(void)
{
    zk_kiosk_t k;
    zk_schedules_t sch;
    char day[24];
    char when[48];
    char name[ZK_NOTE_MAX];
    zk_wall_t now;

    load_home(&k, &sch);
    TEQ_I(k.has_next_run, 1);
    zk_fmt_relative_day(k.next_run.at, k.now, day, sizeof day);
    TEQ_S(day, "Today");
    zk_fmt_fire_when(&k.next_run, k.now, when, sizeof when);
    TEQ_S(when, "Today 8:23 AM");
    zk_fmt_name_upper(k.next_run.name, name, sizeof name);
    TEQ_S(name, "MORNING CYCLE");

    now = wall(2026, 9, 29, 8, 24, 0);
    zk_fmt_relative_day(k.next_run.at, now, day, sizeof day);
    TEQ_S(day, "Today"); /* at is still 08:23 same civil day */

    now = wall(2026, 9, 28, 9, 0, 0);
    zk_fmt_relative_day(k.next_run.at, now, day, sizeof day);
    TEQ_S(day, "Tomorrow");

    now = wall(2026, 9, 24, 9, 0, 0); /* Thu, at is Tue 29 => 5 days */
    zk_fmt_relative_day(k.next_run.at, now, day, sizeof day);
    TEQ_S(day, "Tue");

    now = wall(2026, 9, 20, 9, 0, 0);
    zk_fmt_relative_day(k.next_run.at, now, day, sizeof day);
    TEQ_S(day, "Tue Sep 29");
}

static void test_rain_strip(void)
{
    zk_kiosk_t k;
    memset(&k, 0, sizeof k);
    k.rain_strip.show = 0;
    k.rain_strip.inches = 0.2;
    k.rain_strip.hours = 24;
    TEQ_I(zk_rain_strip_visible(&k), 0);
    k.rain_strip.show = 1;
    k.rain_strip.inches = 0.049;
    k.rain_strip.hours = 24;
    TEQ_I(zk_rain_strip_visible(&k), 1);
    {
        char t[80];
        zk_rain_strip_text(&k.rain_strip, t, sizeof t);
        TEQ_S(t, "0.05 in fell in the last 24 h");
    }
    k.rain_strip.inches = 0.24;
    k.rain_strip.hours = 24;
    {
        char t[80];
        zk_rain_strip_text(&k.rain_strip, t, sizeof t);
        TEQ_S(t, "0.24 in fell in the last 24 h");
    }
    k.rain_strip.hours = 72;
    k.rain_strip.inches = 0.05;
    {
        char t[80];
        zk_rain_strip_text(&k.rain_strip, t, sizeof t);
        TEQ_S(t, "0.05 in fell in the last 72 h");
    }
}

static int plain_number(const char *s)
{
    int dot = 0;
    if (!s || !s[0]) {
        return 0;
    }
    for (; *s; s++) {
        if (*s == '.') {
            if (dot) {
                return 0;
            }
            dot = 1;
            continue;
        }
        if (*s < '0' || *s > '9') {
            return 0;
        }
    }
    return 1;
}

static void expect_inches(double v, const char *want)
{
    char buf[64];
    zk_fmt_inches(v, buf, sizeof buf);
    TEQ_S(buf, want);
    TCHECK(plain_number(buf), "inches [%s]", buf);
    TCHECK(strchr(buf, '-') == NULL, "negative inches [%s]", buf);
}

static void test_inches_extreme(void)
{
    double pos = strtod("1e999", NULL);
    double neg = strtod("-1e999", NULL);
    double nanv = strtod("nan", NULL);
    double big = strtod("1e30", NULL);
    zk_rain_strip_t rs;
    char strip[96];

    expect_inches(pos, "999.99");
    expect_inches(neg, "0.0");
    expect_inches(nanv, "0.0");
    expect_inches(big, "999.99");
    expect_inches(999.99, "999.99");

    memset(&rs, 0, sizeof rs);
    rs.inches = pos;
    rs.hours = 24;
    zk_rain_strip_text(&rs, strip, sizeof strip);
    TCHECK(strstr(strip, "999.99 in fell") == strip, "strip [%s]", strip);
    TCHECK(strchr(strip, '-') == NULL, "strip [%s]", strip);

    rs.inches = neg;
    zk_rain_strip_text(&rs, strip, sizeof strip);
    TCHECK(strchr(strip, '-') == NULL, "neg strip [%s]", strip);
    TCHECK(strstr(strip, "0.0 in fell") != NULL, "neg strip [%s]", strip);

    rs.inches = nanv;
    zk_rain_strip_text(&rs, strip, sizeof strip);
    TCHECK(strstr(strip, "nan") == NULL && strstr(strip, "inf") == NULL, "nan strip [%s]", strip);

    rs.inches = big;
    zk_rain_strip_text(&rs, strip, sizeof strip);
    TCHECK(strstr(strip, "999.99 in fell") == strip, "big strip [%s]", strip);
}

static void test_soil(void)
{
    zk_kiosk_t k;
    char *d = zk_fixture("home-rain", "kiosk.json");
    char *stale = zk_fixture("home-stale", "kiosk.json");
    char *none = zk_fixture("home-nosoil", "kiosk.json");
    TEQ_I(zk_parse_kiosk(d, &k), ZK_OK);
    TEQ_I(zk_station_soil_percent(&k, 0), 58);
    TEQ_I(zk_station_soil_percent(&k, 2), 34);
    TEQ_I(zk_soil_low(zk_station_soil_percent(&k, 2)), 1);
    TEQ_I(zk_soil_low(zk_station_soil_percent(&k, 0)), 0);
    TEQ_I(zk_parse_kiosk(stale, &k), ZK_OK);
    TEQ_I(zk_station_soil_percent(&k, 0), -1);
    TEQ_I(zk_parse_kiosk(none, &k), ZK_OK);
    TEQ_I(zk_station_soil_percent(&k, 0), -1);
    free(d);
    free(stale);
    free(none);
}

static void test_pause_preview(void)
{
    zk_kiosk_t k;
    zk_schedules_t sch;
    zk_pause_preview_t p;
    load_home(&k, &sch);

    TEQ_I(zk_pause_preview(&sch, k.now, ZK_PAUSE_TOMORROW, &p), ZK_OK);
    TEQ_S(p.primary, "Tomorrow morning");
    TEQ_S(p.secondary, "Wed 8:23 AM");
    TEQ_S(p.body, "{\"until\":\"tomorrow_morning\",\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, k.now, ZK_PAUSE_DAYS2, &p), ZK_OK);
    TEQ_S(p.primary, "2 days");
    TEQ_S(p.secondary, "Thu 8:23 AM");
    TEQ_S(p.body, "{\"days\":2,\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, k.now, ZK_PAUSE_WEEK, &p), ZK_OK);
    TEQ_S(p.primary, "1 week");
    TEQ_S(p.secondary, "Tue Oct 6");
    TEQ_S(p.body, "{\"days\":7,\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, k.now, ZK_PAUSE_INDEFINITE, &p), ZK_OK);
    TEQ_S(p.primary, "Until further notice");
    TEQ_S(p.secondary, "Until I resume");
    TEQ_S(p.body, "{\"indefinite\":true,\"reason\":\"kiosk\"}");

    {
        zk_schedules_t empty;
        zk_wall_t cut;
        memset(&empty, 0, sizeof(empty));
        TEQ_I(zk_pause_preview(&empty, k.now, ZK_PAUSE_TOMORROW, &p), ZK_OK);
        TEQ_S(p.secondary, "Wed 6:00 AM");
        cut = zk_morning_cutoff(&empty, 2026, 9, 30);
        TEQ_I(cut.hh, 6);
        TEQ_I(cut.mm, 0);
    }
}

static void test_run_fmt(void)
{
    char *a = zk_fixture("running", "kiosk.json");
    zk_kiosk_t k;
    char mmss[16];
    char step[32];
    TEQ_I(zk_parse_kiosk(a, &k), ZK_OK);
    TEQ_I(k.has_run, 1);
    TEQ_I(k.run.step_index, 1);
    TEQ_I(k.run.step_count, 3);
    TEQ_I(k.run.step_remaining_sec, 161);
    zk_fmt_mmss(k.run.step_remaining_sec, mmss, sizeof mmss);
    TEQ_S(mmss, "2:41");
    zk_fmt_step(k.run.step_index, k.run.step_count, step, sizeof step);
    TEQ_S(step, "Step 2 of 3");
    free(a);
}

static void test_pause_text(void)
{
    char *a = zk_fixture("paused-rain", "kiosk.json");
    char *b = zk_fixture("paused-manual", "kiosk.json");
    zk_kiosk_t k;
    char title[48], until[64];
    TEQ_I(zk_parse_kiosk(a, &k), ZK_OK);
    zk_pause_title(&k.pause, title, sizeof title);
    TEQ_S(title, "PAUSED FOR RAIN");
    zk_pause_until_text(&k.pause, k.now, until, sizeof until);
    TEQ_S(until, "Until Wed 6:00 AM");
    TEQ_I(zk_parse_kiosk(b, &k), ZK_OK);
    zk_pause_title(&k.pause, title, sizeof title);
    TEQ_S(title, "PAUSED");
    zk_pause_until_text(&k.pause, k.now, until, sizeof until);
    TEQ_S(until, "Until you resume");

    memset(&k, 0, sizeof k);
    k.pause.has_until = 1;
    k.pause.until = wall(2026, 10, 2, 6, 0, 0);
    zk_pause_until_text(&k.pause, wall(2026, 9, 25, 15, 0, 0), until, sizeof until);
    TEQ_S(until, "Until Fri Oct 2, 6:00 AM");
    free(a);
    free(b);
}

int main(void)
{
    test_fmt_fire();
    test_rain_strip();
    test_inches_extreme();
    test_soil();
    test_pause_preview();
    test_run_fmt();
    test_pause_text();
    return zk_test_report("test_logic");
}
