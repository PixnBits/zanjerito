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

static zk_schedule_t sched(const char *id, int enabled, const char *start, unsigned wd,
                           int sy, int sm, int sd, int ey, int em, int ed)
{
    zk_schedule_t s;
    int hh = 0, mm = 0;
    memset(&s, 0, sizeof(s));
    zk_str_copy(s.id, sizeof(s.id), id);
    zk_str_copy(s.note, sizeof(s.note), id);
    s.enabled = enabled;
    if (start && sscanf(start, "%d:%d", &hh, &mm) == 2) {
        s.start_hh = hh;
        s.start_mm = mm;
        s.has_start = 1;
    }
    s.weekdays = wd;
    s.n_steps = 1;
    zk_str_copy(s.steps[0].station_id, sizeof(s.steps[0].station_id), "az01");
    s.steps[0].minutes = 1;
    if (sy) {
        s.has_starts_on = 1;
        s.starts_y = sy;
        s.starts_m = sm;
        s.starts_d = sd;
    }
    if (ey) {
        s.has_ends_on = 1;
        s.ends_y = ey;
        s.ends_m = em;
        s.ends_d = ed;
    }
    return s;
}

static void load_home(zk_status_t *st, zk_stations_t *sta, zk_schedules_t *sch, zk_soil_t *so)
{
    char *a = zk_fixture("home-rain", "status.json");
    char *b = zk_fixture("home-rain", "stations.json");
    char *c = zk_fixture("home-rain", "schedules.json");
    char *d = zk_fixture("home-rain", "soil.json");
    TCHECK(a && b && c && d, "home-rain fixtures");
    TEQ_I(zk_parse_status(a, st), ZK_OK);
    TEQ_I(zk_parse_stations(b, sta), ZK_OK);
    TEQ_I(zk_parse_schedules(c, sch), ZK_OK);
    TEQ_I(zk_parse_soil(d, so), ZK_OK);
    free(a);
    free(b);
    free(c);
    free(d);
}

static void test_next_run_tables(void)
{
    zk_status_t st;
    zk_stations_t sta;
    zk_schedules_t sch;
    zk_soil_t so;
    zk_next_run_t nr;
    zk_schedules_t one;
    zk_wall_t now;

    load_home(&st, &sta, &sch, &so);

    /* Tue 06:52 + 08:23 => Today */
    TEQ_I(zk_next_run(&sch, &sta, st.now, &nr), ZK_OK);
    TEQ_I(nr.have, 1);
    TEQ_S(nr.day, "Today");
    TEQ_S(nr.time, "8:23 AM");
    TEQ_S(nr.name, "MORNING CYCLE");
    TEQ_S(nr.summary, "Daily 8:23 AM");

    /* after 08:23 => Tomorrow */
    now = wall(2026, 9, 29, 8, 23, 0);
    TEQ_I(zk_next_run(&sch, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Tomorrow");
    TEQ_S(nr.time, "8:23 AM");
    now = wall(2026, 9, 29, 8, 24, 0);
    TEQ_I(zk_next_run(&sch, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Tomorrow");

    /* disabled skipped: only drip-weekly (disabled) would be Thu 7:31 */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sch.items[1]; /* disabled */
    TEQ_I(zk_next_run(&one, &sta, st.now, &nr), ZK_OK);
    TEQ_I(nr.have, 0);

    /* weekday wrap: Thursday-only from Tuesday */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("thu", 1, "07:31", ZK_WD_THU, 0, 0, 0, 0, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, st.now, &nr), ZK_OK);
    TEQ_I(nr.have, 1);
    TEQ_S(nr.day, "Thu");
    TEQ_S(nr.time, "7:31 AM");
    TEQ_S(nr.summary, "Thu 7:31 AM");

    /* beyond 6 days: Thu-only after Thursday start */
    now = wall(2026, 10, 1, 9, 0, 0); /* Thu Oct 1 after 7:31 */
    TEQ_I(now.wday, 4);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Thu Oct 8");

    /* starts_on in future */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("later", 1, "08:23", ZK_WD_ALL, 2026, 10, 10, 0, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, st.now, &nr), ZK_OK);
    TEQ_S(nr.day, "Sat Oct 10");

    /* ends_on passed */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("past", 1, "08:23", ZK_WD_ALL, 0, 0, 0, 2026, 9, 1);
    TEQ_I(zk_next_run(&one, &sta, st.now, &nr), ZK_OK);
    TEQ_I(nr.have, 0);

    /* later today vs tomorrow already covered; two starts 06:52 and 08:23 at 06:52 */
    memset(&one, 0, sizeof(one));
    one.n = 2;
    one.items[0] = sched("early", 1, "06:52", ZK_WD_ALL, 0, 0, 0, 0, 0, 0);
    one.items[1] = sched("late", 1, "08:23", ZK_WD_ALL, 0, 0, 0, 0, 0, 0);
    now = wall(2026, 9, 29, 6, 52, 0);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Today");
    TEQ_S(nr.time, "8:23 AM");

    /* month/year rollover */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("ny", 1, "08:00", ZK_WD_ALL, 0, 0, 0, 0, 0, 0);
    now = wall(2026, 12, 31, 9, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Tomorrow");
    TEQ_S(nr.time, "8:00 AM");

    /* leap day */
    now = wall(2024, 2, 28, 9, 0, 0);
    TEQ_I(now.wday, 3); /* Wed */
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Tomorrow"); /* Thu Feb 29 */
    now = wall(2024, 2, 29, 9, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.day, "Tomorrow"); /* Fri Mar 1 */

    /* weekdays summary */
    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("wd", 1, "06:00", ZK_WD_MON | ZK_WD_TUE | ZK_WD_WED | ZK_WD_THU | ZK_WD_FRI, 0, 0, 0, 0, 0, 0);
    now = wall(2026, 9, 29, 5, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.summary, "Weekdays 6:00 AM");

    memset(&one, 0, sizeof(one));
    one.n = 1;
    one.items[0] = sched("mw", 1, "06:00", ZK_WD_MON | ZK_WD_WED, 0, 0, 0, 0, 0, 0);
    TEQ_I(zk_next_run(&one, &sta, now, &nr), ZK_OK);
    TEQ_S(nr.summary, "Mon Wed 6:00 AM");
}

static void test_rain_strip(void)
{
    zk_status_t st;
    memset(&st, 0, sizeof(st));
    st.rain.enabled = 1;
    st.rain.unavailable = 0;
    st.rain.have_totals = 1;
    st.rain.total_72h = 0.049;
    st.rain.total_24h = 0.049;
    TEQ_I(zk_rain_strip_visible(&st), 0);
    st.rain.total_72h = 0.05;
    st.rain.total_24h = 0.0;
    TEQ_I(zk_rain_strip_visible(&st), 1);
    {
        char t[80];
        zk_rain_strip_text(&st, t, sizeof(t));
        TEQ_S(t, "0.05 in fell in the last 72 hours");
    }
    st.rain.total_72h = 0.24;
    st.rain.total_24h = 0.24;
    TEQ_I(zk_rain_strip_visible(&st), 1);
    {
        char t[80];
        zk_rain_strip_text(&st, t, sizeof(t));
        TEQ_S(t, "0.24 in fell in the last 24 hours");
    }
    st.rain.enabled = 0;
    TEQ_I(zk_rain_strip_visible(&st), 0);
    st.rain.enabled = 1;
    st.rain.unavailable = 1;
    TEQ_I(zk_rain_strip_visible(&st), 0);
    st.rain.unavailable = 0;
    st.rain.have_totals = 0;
    TEQ_I(zk_rain_strip_visible(&st), 0);
}

static void test_soil(void)
{
    zk_soil_t so;
    char *d = zk_fixture("home-rain", "soil.json");
    char *stale = zk_fixture("home-stale", "soil.json");
    char *none = zk_fixture("home-nosoil", "soil.json");
    TEQ_I(zk_parse_soil(d, &so), ZK_OK);
    TEQ_I(zk_soil_percent(&so, "az01"), 58);
    TEQ_I(zk_soil_percent(&so, "az03"), 34);
    TEQ_I(zk_soil_low(zk_soil_percent(&so, "az03")), 1);
    TEQ_I(zk_soil_low(zk_soil_percent(&so, "az01")), 0);
    TEQ_I(zk_parse_soil(stale, &so), ZK_OK);
    TEQ_I(zk_soil_percent(&so, "az01"), -1);
    TEQ_I(zk_parse_soil(none, &so), ZK_OK);
    TEQ_I(zk_soil_percent(&so, "az01"), -1);
    TEQ_I(zk_parse_soil("{\"enabled\":true,\"et_known\":true,\"et_stale\":false,\"zones\":[{\"station_id\":\"az01\",\"percent\":70,\"rate_measured\":false}]}", &so), ZK_OK);
    TEQ_I(zk_soil_percent(&so, "az01"), -1);
    TEQ_I(zk_parse_soil("{\"enabled\":true,\"et_known\":true,\"et_stale\":false,\"zones\":[{\"station_id\":\"az01\",\"percent\":null,\"rate_measured\":true}]}", &so), ZK_OK);
    TEQ_I(zk_soil_percent(&so, "az01"), -1);
    free(d);
    free(stale);
    free(none);
}

static void test_pause_preview(void)
{
    zk_status_t st;
    zk_stations_t sta;
    zk_schedules_t sch;
    zk_soil_t so;
    zk_pause_preview_t p;
    load_home(&st, &sta, &sch, &so);

    TEQ_I(zk_pause_preview(&sch, st.now, ZK_PAUSE_TOMORROW, &p), ZK_OK);
    TEQ_S(p.primary, "Tomorrow morning");
    TEQ_S(p.secondary, "Wed 8:23 AM");
    TEQ_S(p.body, "{\"until\":\"tomorrow_morning\",\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, st.now, ZK_PAUSE_DAYS2, &p), ZK_OK);
    TEQ_S(p.primary, "2 days");
    TEQ_S(p.secondary, "Thu 8:23 AM");
    TEQ_S(p.body, "{\"days\":2,\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, st.now, ZK_PAUSE_WEEK, &p), ZK_OK);
    TEQ_S(p.primary, "1 week");
    TEQ_S(p.secondary, "Tue Oct 6");
    TEQ_S(p.body, "{\"days\":7,\"reason\":\"kiosk\"}");

    TEQ_I(zk_pause_preview(&sch, st.now, ZK_PAUSE_INDEFINITE, &p), ZK_OK);
    TEQ_S(p.primary, "Until further notice");
    TEQ_S(p.secondary, "Until I resume");
    TEQ_S(p.body, "{\"indefinite\":true,\"reason\":\"kiosk\"}");

    /* no schedules => 06:00 */
    {
        zk_schedules_t empty;
        zk_wall_t cut;
        memset(&empty, 0, sizeof(empty));
        TEQ_I(zk_pause_preview(&empty, st.now, ZK_PAUSE_TOMORROW, &p), ZK_OK);
        TEQ_S(p.secondary, "Wed 6:00 AM");
        cut = zk_morning_cutoff(&empty, 2026, 9, 30);
        TEQ_I(cut.hh, 6);
        TEQ_I(cut.mm, 0);
    }
}

static void test_run_infer(void)
{
    char *a = zk_fixture("running", "status.json");
    char *c = zk_fixture("running", "schedules.json");
    zk_status_t st;
    zk_schedules_t sch;
    zk_run_info_t info;
    char mmss[16];
    char step[32];
    TEQ_I(zk_parse_status(a, &st), ZK_OK);
    TEQ_I(zk_parse_schedules(c, &sch), ZK_OK);
    TEQ_I(zk_run_infer(&st, &sch, st.now, &info), ZK_OK);
    TEQ_I(info.have, 1);
    TEQ_I(info.step_index, 1);
    TEQ_I(info.step_count, 3);
    TEQ_I(info.remaining_sec, 161);
    TEQ_I(info.total_step_sec, 180);
    TEQ_S(info.current_title_id, "az02");
    TEQ_S(info.next_station_id, "az04");
    zk_fmt_mmss(info.remaining_sec, mmss, sizeof(mmss));
    TEQ_S(mmss, "2:41");
    zk_fmt_step(info.step_index, info.step_count, step, sizeof(step));
    TEQ_S(step, "Step 2 of 3");
    free(a);
    free(c);

    /* unknown: watering but station not in itinerary */
    memset(&st, 0, sizeof(st));
    st.watering = 1;
    zk_str_copy(st.current_station, sizeof(st.current_station), "nope");
    TEQ_I(zk_run_infer(&st, &sch, wall(2026, 9, 29, 8, 25, 19), &info), ZK_OK);
    TEQ_I(info.have, 1);
    TEQ_I(info.step_count, 1);
    TEQ_I(info.remaining_sec, -1);
}

static void test_pause_text(void)
{
    char *a = zk_fixture("paused-rain", "status.json");
    char *b = zk_fixture("paused-manual", "status.json");
    zk_status_t st;
    char title[48], until[64];
    TEQ_I(zk_parse_status(a, &st), ZK_OK);
    zk_pause_title(&st, title, sizeof(title));
    TEQ_S(title, "PAUSED FOR RAIN");
    zk_pause_until_text(&st, st.now, until, sizeof(until));
    TEQ_S(until, "Until Wed 6:00 AM");
    TEQ_I(zk_parse_status(b, &st), ZK_OK);
    zk_pause_title(&st, title, sizeof(title));
    TEQ_S(title, "PAUSED");
    zk_pause_until_text(&st, st.now, until, sizeof(until));
    TEQ_S(until, "Until you resume");

    /* beyond 6 days */
    memset(&st, 0, sizeof(st));
    st.has_paused_until = 1;
    st.paused_until = wall(2026, 10, 2, 6, 0, 0);
    zk_pause_until_text(&st, wall(2026, 9, 25, 15, 0, 0), until, sizeof(until));
    TEQ_S(until, "Until Fri Oct 2, 6:00 AM");
    free(a);
    free(b);
}

int main(void)
{
    test_next_run_tables();
    test_rain_strip();
    test_soil();
    test_pause_preview();
    test_run_infer();
    test_pause_text();
    return zk_test_report("test_logic");
}
