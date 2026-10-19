#include "zk_test.h"

#include "zk_copy.h"

#include <stdlib.h>
#include "zk_flow.h"
#include "zk_layout.h"
#include "zk_logic.h"
#include "zk_repaint.h"
#include "zk_soilcfg.h"

static void wall_at(zk_wall_t *w, int y, int m, int d, int hh, int mm)
{
    memset(w, 0, sizeof *w);
    w->y = y;
    w->m = m;
    w->d = d;
    w->hh = hh;
    w->mm = mm;
    zk_wall_set_wday(w);
}

static void add_station(zk_kiosk_t *k, const char *id, const char *title, int pct)
{
    zk_kiosk_station_t *st;
    if (k->n_stations >= ZK_MAX_STATIONS) {
        return;
    }
    st = &k->stations[k->n_stations++];
    zk_str_copy(st->id, sizeof st->id, id);
    zk_str_copy(st->title, sizeof st->title, title);
    st->soil_percent = pct;
}

static void base_yard(zk_kiosk_t *k, int n)
{
    memset(k, 0, sizeof *k);
    wall_at(&k->now, 2026, 10, 19, 18, 0);
    k->has_now = 1;
    zk_str_copy(k->phase, sizeof k->phase, "Idle");
    zk_str_copy(k->timezone, sizeof k->timezone, "America/Denver");
    add_station(k, "lawn-a", "Lawn A", -1);
    add_station(k, "lawn-b", "Lawn B", -1);
    add_station(k, "drip", "Front Yard Drip Emitters", -1);
    add_station(k, "beds", "Beds", -1);
    if (n > 4) {
        add_station(k, "trees", "Trees", -1);
        add_station(k, "side", "Side Yard Roses", -1);
    }
    while (k->n_stations > n) {
        k->n_stations--;
    }
}

static void next_weekdays(zk_kiosk_t *k, zk_schedules_t *sc)
{
    zk_schedule_t *s;
    memset(sc, 0, sizeof *sc);
    k->has_next_run = 1;
    zk_str_copy(k->next_run.schedule_id, sizeof k->next_run.schedule_id, "weekdays");
    zk_str_copy(k->next_run.name, sizeof k->next_run.name, "Lawn A + B " ZK_MID " Weekdays AM");
    wall_at(&k->next_run.at, 2026, 10, 20, 6, 10);
    k->next_run.has_at = 1;
    k->next_run.total_min = 18;
    s = &sc->items[sc->n++];
    zk_str_copy(s->id, sizeof s->id, "weekdays");
    zk_str_copy(s->name, sizeof s->name, "Lawn A + B " ZK_MID " Weekdays AM");
    s->enabled = 1;
    s->has_start = 1;
    s->start_hh = 6;
    s->start_mm = 10;
    s->weekdays = 0x7F;
    zk_str_copy(s->steps[0].station_id, ZK_ID_MAX, "lawn-a");
    s->steps[0].minutes = 6;
    zk_str_copy(s->steps[1].station_id, ZK_ID_MAX, "lawn-b");
    s->steps[1].minutes = 6;
    zk_str_copy(s->steps[2].station_id, ZK_ID_MAX, "drip");
    s->steps[2].minutes = 6;
    s->n_steps = 3;
}

static void build(const zk_kiosk_t *k, const zk_schedules_t *sc, const zk_view_in_t *in, int unreach, zk_view_t *v)
{
    zk_copy_src_t src;
    memset(&src, 0, sizeof src);
    src.kiosk = k;
    src.have_kiosk = k ? 1 : 0;
    src.schedules = sc;
    src.have_schedules = sc && sc->n > 0;
    src.unreachable = unreach;
    if (k && k->has_now) {
        src.wall = k->now;
    }
    zk_view_build(&src, in, v);
}

static int byte_px(const char *text, size_t n, void *user)
{
    (void)text;
    (void)user;
    return (int)n;
}

static int too_wide(const char *text, size_t n, void *user)
{
    (void)text;
    (void)n;
    (void)user;
    return 800;
}

static void test_copy_states(void)
{
    zk_kiosk_t k;
    zk_schedules_t sc;
    zk_view_in_t in;
    zk_view_t v;
    char joined[512];
    const char *nbsp = "" ZK_NBSP "";

    base_yard(&k, 4);
    next_weekdays(&k, &sc);
    memset(&in, 0, sizeof in);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, "NEXT RUN " ZK_MID " LAWN A + B " ZK_MID " WEEKDAYS AM");
    TEQ_S(v.hero.headline, "Tomorrow 6:10" ZK_NBSP "AM");
    TEQ_S(v.hero.secondary, "3 stations " ZK_MID " about 18 min");
    TCHECK(strstr(v.hero.headline, nbsp) != NULL, "nbsp before AM");
    TCHECK(v.hero.headline[0] != '0', "no leading zero");
    TEQ_I((int)v.hero.style, (int)ZK_HERO_IDLE);
    TEQ_S(v.bar_word, "Pause for rain");
    TEQ_I(v.stop_filled, 0);
    TEQ_I(zk_usual_minutes(&(zk_copy_src_t){.kiosk = &k, .have_kiosk = 1, .schedules = &sc, .have_schedules = 1}, 2),
          6);

    k.rain.have_totals = 1;
    k.rain.total_72h = 0.4;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.4 in " ZK_MID " last 72 h");
    TEQ_I(v.hero.rain, 1);
    TEQ_I(v.hero.rain_fill_px, 131);
    /* Gate is half-up to 0.01 in, at least 0.06. Words are half-up to 0.1 in. */
    k.rain.total_72h = 0.05;
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.hero.rain, 0);
    k.rain.total_72h = 0.054;
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.hero.rain, 0);
    k.rain.total_72h = 0.055;
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.hero.rain, 1);
    TEQ_S(v.hero.rain_text, "Rain 0.1 in " ZK_MID " last 72 h");
    k.rain.total_72h = 0.06;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.1 in " ZK_MID " last 72 h");
    k.rain.total_72h = 0.14;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.1 in " ZK_MID " last 72 h");
    k.rain.total_72h = 0.15;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.2 in " ZK_MID " last 72 h");
    k.rain.total_72h = 0.24;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.2 in " ZK_MID " last 72 h");
    TCHECK(strstr(v.hero.rain_text, "0.0") == NULL, "rain line said 0.0");
    k.rain.total_72h = 0.25;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 0.3 in " ZK_MID " last 72 h");
    k.rain.total_72h = 1.24;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.rain_text, "Rain 1.2 in " ZK_MID " last 72 h");
    TEQ_I(v.hero.rain_fill_px, ZK_RAIN_TRACK);
    k.rain.total_72h = 0.03;
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.hero.rain, 0);
    k.rain.have_totals = 0;

    /* Manual run wins over a next run. */
    k.has_run = 1;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "manual");
    zk_str_copy(k.run.program, sizeof k.run.program, "manual");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "lawn-a");
    k.run.step_remaining_sec = 240;
    k.watering = 1;
    wall_at(&k.now, 2026, 10, 19, 15, 30);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, "MANUAL RUN " ZK_MID " LAWN A");
    TEQ_S(v.hero.headline, "Lawn A");
    TEQ_S(v.hero.time_line, "4 min left");
    TEQ_S(v.hero.secondary, "ends 3:34" ZK_NBSP "PM");
    TEQ_I((int)v.hero.style, (int)ZK_HERO_MANUAL);
    TEQ_I((int)v.tiles[0].vis, (int)ZK_TILE_ON);
    TEQ_S(v.tiles[0].slot, "4 min left");
    TEQ_I(v.stop_filled, 1);

    /* Watering. */
    memset(&k.run, 0, sizeof k.run);
    k.has_run = 1;
    k.watering = 1;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.program, sizeof k.run.program, "Lawn A + B " ZK_MID " Weekdays AM");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "drip");
    zk_str_copy(k.run.next_station, sizeof k.run.next_station, "beds");
    k.run.step_remaining_sec = 120;
    wall_at(&k.now, 2026, 10, 20, 6, 26);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, "WATERING NOW " ZK_MID " LAWN A + B " ZK_MID " WEEKDAYS AM");
    TEQ_S(v.hero.headline, "Front Yard Drip Emitters");
    TEQ_I((int)strlen("Front Yard Drip Emitters"), 24);
    TCHECK(strstr(v.hero.headline, "\xE2\x80\xA6") == NULL, "24-char name truncated in copy");
    TEQ_S(v.hero.time_line, "2 min left");
    TEQ_S(v.hero.secondary, "ends 6:28" ZK_NBSP "AM " ZK_MID " next Beds");
    TEQ_I((int)v.tiles[2].vis, (int)ZK_TILE_ON);
    TEQ_S(v.tiles[2].slot, "2 min left");
    TEQ_I(v.stop_filled, 1);

    /* Paused. */
    k.has_run = 0;
    k.watering = 0;
    memset(&k.run, 0, sizeof k.run);
    k.pause.paused = 1;
    k.pause.has_until = 1;
    wall_at(&k.pause.until, 2026, 10, 24, 6, 0);
    k.pause.until.wday = 6;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, "PAUSED FOR RAIN");
    TEQ_S(v.hero.headline, "Until Sat 6:00" ZK_NBSP "AM");
    TEQ_S(v.hero.secondary, "Tap a station to run it anyway");
    TEQ_S(v.hero.time_line, "");
    TEQ_S(v.bar_word, "Resume");
    TEQ_I(v.bar_is_resume, 1);
    /* Clock is Monday. Pause until Wednesday 6:00, so the next 6:10 is Wednesday,
     * not the skipped Tuesday fire ("Tomorrow"). */
    wall_at(&k.now, 2026, 10, 19, 18, 0);
    wall_at(&k.pause.until, 2026, 10, 21, 6, 0);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.headline, "Until Wed 6:00" ZK_NBSP "AM");
    TEQ_S(v.hero.time_line, "");
    TEQ_S(v.hero.secondary, "Tap a station to run it anyway");
    {
        zk_wall_t until;
        zk_wall_t fire;
        wall_at(&until, 2026, 10, 21, 6, 10);
        TEQ_I(zk_next_fire_after(&sc, until, &fire), 1);
        TEQ_I(fire.d, 21);
        TEQ_I(fire.hh, 6);
        TEQ_I(fire.mm, 10);
        wall_at(&until, 2026, 10, 21, 6, 11);
        TEQ_I(zk_next_fire_after(&sc, until, &fire), 1);
        TEQ_I(fire.d, 22);
        TEQ_I(fire.hh, 6);
        TEQ_I(fire.mm, 10);
    }
    k.pause.has_until = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.headline, "Until you resume");
    TEQ_S(v.hero.time_line, "");

    /* Nothing scheduled. */
    k.pause.paused = 0;
    k.has_next_run = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, "SCHEDULE");
    TEQ_S(v.hero.headline, "Nothing scheduled");
    TEQ_S(v.hero.secondary, "Add a program on your phone");

    /* Offline, pending, confirmed. */
    base_yard(&k, 6);
    next_weekdays(&k, &sc);
    wall_at(&k.now, 2026, 10, 20, 6, 42);
    build(&k, &sc, &in, 1, &v);
    TEQ_S(v.hero.label, "LAST UPDATE 6:42" ZK_NBSP "AM " ZK_MID " RETRYING");
    TEQ_S(v.hero.headline, "Can't reach controller");
    TEQ_S(v.hero.secondary, "Check the controller power and Wi-Fi");
    TEQ_S(v.tiles[0].slot, "unavailable");
    TEQ_I((int)v.tiles[0].vis, (int)ZK_TILE_UNAVAILABLE);
    TEQ_I(v.bar_disabled, 1);
    TEQ_I(v.stop_filled, 0);
    TCHECK(strstr(v.hero.headline, "Watering stopped") == NULL, "offline is not stopped");

    in.stop_phase = ZK_STOP_QUEUED;
    in.offline_pending = 1;
    build(&k, &sc, &in, 1, &v);
    TEQ_S(v.hero.headline, ZK_STOP_PENDING_HEADLINE);
    TEQ_S(v.hero.secondary, ZK_STOP_PENDING_OFFLINE);
    snprintf(joined, sizeof joined, "%s\n%s", v.hero.headline, v.hero.secondary);
    TEQ_S(joined, ZK_STOP_PENDING_HEADLINE "\n" ZK_STOP_PENDING_OFFLINE);
    TCHECK(strstr(joined, "sent yet") == NULL, "retired sent-yet copy");
    TCHECK(strstr(v.hero.label, "NO REPLY") == NULL, "retired no-reply label");
    TEQ_S(v.hero.label, "STOP " ZK_MID " LAST UPDATE 6:42" ZK_NBSP "AM");
    TCHECK(strcmp(v.hero.headline, "Watering stopped") != 0, "pending is not stopped");
    TEQ_I(v.stop_filled, 1);
    TEQ_I(v.bar_disabled, 1);
    TEQ_I((int)v.tiles[0].vis, (int)ZK_TILE_UNAVAILABLE);
    TEQ_I(zk_stop_shows_stopped(&(zk_stop_flow_t){.phase = ZK_STOP_QUEUED}), 0);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.label, ZK_STOP_PENDING_LABEL);
    TEQ_S(v.hero.headline, ZK_STOP_PENDING_HEADLINE);
    TEQ_S(v.hero.secondary, ZK_STOP_PENDING_RETRY);
    TEQ_I((int)v.hero.style, (int)ZK_HERO_STOP_PENDING);
    TEQ_I(v.stop_filled, 1);
    TEQ_I(v.bar_disabled, 1);
    TEQ_I((int)v.tiles[0].vis, (int)ZK_TILE_UNAVAILABLE);

    memset(&in, 0, sizeof in);
    in.stop_phase = ZK_STOP_CONFIRMED;
    in.has_confirmed_at = 1;
    wall_at(&in.confirmed_at, 2026, 10, 20, 6, 44);
    wall_at(&k.now, 2026, 10, 20, 6, 44);
    wall_at(&k.next_run.at, 2026, 10, 21, 6, 10);
    k.next_run.has_at = 1;
    k.has_next_run = 1;
    build(&k, &sc, &in, 1, &v);
    TEQ_S(v.hero.label, "STOP " ZK_MID " CONFIRMED 6:44" ZK_NBSP "AM");
    TEQ_S(v.hero.headline, "Watering stopped");
    TEQ_S(v.hero.secondary, "Next run Tomorrow 6:10" ZK_NBSP "AM");

    k.pause.paused = 1;
    k.has_next_effective = 1;
    k.next_effective.has_at = 1;
    wall_at(&k.next_effective.at, 2026, 10, 21, 6, 10);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.headline, "Watering stopped");
    TEQ_S(v.hero.secondary, "Next run Tomorrow 6:10" ZK_NBSP "AM");
    k.has_next_effective = 0;
    k.next_effective.has_at = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.hero.secondary, "Paused until you resume");
    TCHECK(strcmp(v.hero.headline, "Watering stopped") == 0, "paused stop headline");
}

static void test_copy_sheets(void)
{
    zk_kiosk_t k;
    zk_schedules_t sc;
    zk_view_in_t in;
    zk_view_t v;
    zk_schedule_t *drip;

    base_yard(&k, 4);
    next_weekdays(&k, &sc);
    memset(&in, 0, sizeof in);
    in.sheet_kind = ZK_SHEET_RUN;
    in.station_index = 2;
    in.minutes = 6;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "RUN A STATION");
    TEQ_S(v.sheet.title, "Run Front Yard Drip Emitters for 6 min?");
    TEQ_S(v.sheet.body, "Runs once now. Schedules don't change.");
    TEQ_I(v.sheet.show_stepper, 1);
    TEQ_I(v.sheet.minus_enabled, 1);
    in.minutes = 1;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.title, "Run Front Yard Drip Emitters for 1 min?");
    TEQ_I(v.sheet.minus_enabled, 0);

    k.pause.paused = 1;
    k.pause.has_until = 1;
    wall_at(&k.pause.until, 2026, 10, 24, 6, 0);
    k.pause.until.wday = 6;
    in.sheet_kind = ZK_SHEET_ANYWAY;
    in.station_index = 0;
    in.minutes = 6;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "PAUSED FOR RAIN");
    TEQ_S(v.sheet.title, "Run Lawn A anyway?");
    TEQ_S(v.sheet.body, "Paused until Sat 6:00" ZK_NBSP "AM. Runs once; the pause stays on.");
    TCHECK(strstr(v.sheet.body, "pause stays on") != NULL, "pause stays");

    k.pause.paused = 0;
    k.has_run = 1;
    k.watering = 1;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.program, sizeof k.run.program, "Front Summer");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "drip");
    zk_str_copy(k.run.next_station, sizeof k.run.next_station, "beds");
    k.run.step_remaining_sec = 120;
    in.sheet_kind = ZK_SHEET_STOP_STATION;
    in.station_index = 2;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "WATERING NOW " ZK_MID " FRONT SUMMER");
    TEQ_S(v.sheet.title, "Front Yard Drip Emitters is watering");
    TEQ_S(v.sheet.time_line, "2 min left " ZK_MID " ends 6:02" ZK_NBSP "PM");
    TEQ_S(v.sheet.body, "Next: Beds. Tap STOP below to end all watering.");
    k.run.next_station[0] = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.title, "Front Yard Drip Emitters is watering");
    TEQ_S(v.sheet.body, "Last station. Tap STOP below to end watering now.");
    in.width_px = too_wide;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.body, "Tap STOP below to end all watering.");
    in.width_px = NULL;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "manual");
    zk_str_copy(k.run.program, sizeof k.run.program, "manual");
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "MANUAL RUN");
    TEQ_S(v.sheet.body, "Manual run. Tap STOP below to end it now.");
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.program, sizeof k.run.program, "Front Summer");

    in.sheet_kind = ZK_SHEET_STOP_ALL;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "STOP");
    TEQ_S(v.sheet.title, "Stop all watering?");
    TEQ_S(v.sheet.body, "Front Yard Drip Emitters closes now and the rest of Front Summer is skipped. Schedules stay on.");

    zk_str_copy(k.run.kind, sizeof k.run.kind, "manual");
    zk_str_copy(k.run.program, sizeof k.run.program, "manual");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "lawn-a");
    in.station_index = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.title, "Stop all watering?");
    TEQ_S(v.sheet.body, "Ends the manual run on Lawn A now. Schedules stay on.");
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.program, sizeof k.run.program, "Front Summer");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "drip");

    in.offline_pending = 1;
    build(&k, &sc, &in, 1, &v);
    TEQ_S(v.sheet.label, "STOP " ZK_MID " CAN'T REACH CONTROLLER");
    TEQ_S(v.sheet.title, "Stop all watering?");
    TCHECK(strstr(v.sheet.body, "The controller can't be reached right now.") == v.sheet.body, "offline stop lead");
    TCHECK(strstr(v.sheet.body, "'Watering stopped'") != NULL, "quotes the confirmed line");

    /* Nothing running. */
    k.has_run = 0;
    k.watering = 0;
    k.n_on = 0;
    k.current_station[0] = 0;
    memset(&k.run, 0, sizeof k.run);
    in.sheet_kind = ZK_SHEET_STOP_ALL;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.title, "Stop all watering?");
    TEQ_S(v.sheet.body, "Nothing is watering right now. Schedules stay on.");

    /* Overlap detail uses the spec's example program names. */
    memset(&k, 0, sizeof k);
    base_yard(&k, 6);
    wall_at(&k.now, 2026, 10, 19, 18, 0);
    k.has_now = 1;
    k.has_next_run = 1;
    zk_str_copy(k.next_run.name, sizeof k.next_run.name, "Front Summer");
    zk_str_copy(k.next_run.schedule_id, sizeof k.next_run.schedule_id, "front-summer");
    wall_at(&k.next_run.at, 2026, 10, 20, 6, 10);
    k.next_run.has_at = 1;
    k.next_run.total_min = 30;
    memset(&sc, 0, sizeof sc);
    zk_str_copy(sc.items[0].id, ZK_ID_MAX, "front-summer");
    zk_str_copy(sc.items[0].name, ZK_NOTE_MAX, "Front Summer");
    sc.items[0].enabled = 1;
    sc.items[0].has_start = 1;
    sc.items[0].start_hh = 6;
    sc.items[0].start_mm = 10;
    sc.items[0].weekdays = 1u << k.next_run.at.wday;
    zk_str_copy(sc.items[0].steps[0].station_id, ZK_ID_MAX, "lawn-a");
    sc.items[0].steps[0].minutes = 30;
    sc.items[0].n_steps = 1;
    drip = &sc.items[1];
    zk_str_copy(drip->id, ZK_ID_MAX, "drip-year");
    zk_str_copy(drip->name, ZK_NOTE_MAX, "Drip " ZK_MID " All year");
    drip->enabled = 1;
    drip->has_start = 1;
    drip->start_hh = 6;
    drip->start_mm = 30;
    drip->weekdays = sc.items[0].weekdays;
    zk_str_copy(drip->steps[0].station_id, ZK_ID_MAX, "drip");
    drip->steps[0].minutes = 10;
    drip->n_steps = 1;
    sc.n = 2;
    memset(&in, 0, sizeof in);
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.overlap, 1);
    TCHECK(strstr(v.hero.secondary, "Fix on phone") != NULL, "overlap line");
    TCHECK(strstr(v.hero.secondary, "Overlap on ") == v.hero.secondary, "overlap lead");
    in.sheet_kind = ZK_SHEET_OVERLAP;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sheet.label, "OVERLAP " ZK_MID " READ ONLY");
    TEQ_S(v.sheet.title, "Two programs overlap on Tue");
    TEQ_S(v.sheet.body,
          "Front Summer (6:10" ZK_NBSP "AM " ZK_EN " 6:40" ZK_NBSP "AM) overlaps Drip " ZK_MID " All year (6:30" ZK_NBSP "AM). "
          "Stations would wait their turn. Fix on phone: Zanjerito " ZK_RQ " Schedules.");
    TEQ_S(ZK_WORD_CANCEL, "Cancel");
    TEQ_S(ZK_WORD_RUN, "Run");
    TEQ_S(ZK_WORD_STOP, "STOP");
    TEQ_S(ZK_WORD_STOP_ALL, "Stop all");
    TEQ_S(ZK_WORD_OK, "OK");
    TEQ_S(ZK_WORD_PAUSE, "Pause for rain");
    TEQ_S(ZK_WORD_RESUME, "Resume");
}

static void test_label_clamp_headline_intact(void)
{
    zk_kiosk_t k;
    zk_schedules_t sc;
    zk_view_in_t in;
    zk_view_t v;
    char long_name[80];
    memset(long_name, 'M', 60);
    long_name[60] = 0;
    base_yard(&k, 4);
    next_weekdays(&k, &sc);
    zk_str_copy(k.next_run.name, sizeof k.next_run.name, long_name);
    k.has_run = 1;
    k.watering = 1;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.program, sizeof k.run.program, "Morning cycle");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "drip");
    zk_str_copy(k.stations[2].title, sizeof k.stations[2].title,
                "Front Yard Drip Emitters Front Yard Drip Emitters Front Yard");
    memset(&in, 0, sizeof in);
    in.label_px = 8;
    in.tile_px = 8;
    in.width_px = byte_px;
    build(&k, &sc, &in, 0, &v);
    TCHECK(strstr(v.hero.label, "" "\xE2\x80\xA6" "") != NULL, "label ellipsizes: %s", v.hero.label);
    TCHECK(strstr(v.hero.headline, "" "\xE2\x80\xA6" "") == NULL, "headline is not truncated: %s", v.hero.headline);
    TEQ_S(v.hero.headline, k.stations[2].title);
    TCHECK(strstr(v.tiles[2].name, "" "\xE2\x80\xA6" "") != NULL, "tile name clamps");
}

static void test_tile_tap_never_runs(void)
{
    zk_flow_out_t o;
    zk_flow_tile_t tile;
    int id;
    memset(&tile, 0, sizeof tile);
    for (id = ZK_TARGET_TILE0; id <= ZK_TARGET_TILE5; id++) {
        zk_flow_tap((enum zk_target_id)id, &tile, &o);
        TCHECK(o.effect != ZK_TAP_SUBMIT_RUN, "idle tile %d submitted", id);
        TEQ_I((int)o.effect, (int)ZK_TAP_OPEN_RUN);
        tile.paused = 1;
        zk_flow_tap((enum zk_target_id)id, &tile, &o);
        TCHECK(o.effect != ZK_TAP_SUBMIT_RUN, "paused tile submitted");
        TEQ_I((int)o.effect, (int)ZK_TAP_OPEN_ANYWAY);
        tile.paused = 0;
        tile.tile_on = 1;
        zk_flow_tap((enum zk_target_id)id, &tile, &o);
        TCHECK(o.effect != ZK_TAP_SUBMIT_RUN, "running tile submitted a run");
        TCHECK(o.effect != ZK_TAP_SUBMIT_STOP, "running tile submitted a stop");
        TEQ_I((int)o.effect, (int)ZK_TAP_OPEN_STOP_STATION);
        tile.tile_on = 0;
    }
    zk_flow_tap(ZK_TARGET_RUN, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_SUBMIT_RUN);
    zk_flow_tap(ZK_TARGET_STOP, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_OPEN_STOP_ALL);
    zk_flow_tap(ZK_TARGET_STOP_ALL, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_SUBMIT_STOP);
    zk_flow_tap(ZK_TARGET_CANCEL, &tile, &o);
    TCHECK(o.effect != ZK_TAP_SUBMIT_RUN, "cancel submitted a run");
    TCHECK(o.effect != ZK_TAP_SUBMIT_STOP, "cancel submitted a stop");
    TEQ_I((int)o.effect, (int)ZK_TAP_CLOSE);
    zk_flow_tap(ZK_TARGET_PAUSE, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_SUBMIT_PAUSE);
    zk_flow_tap(ZK_TARGET_SCHEDULES, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_OPEN_SCHEDULES);
    TCHECK(o.effect != ZK_TAP_SUBMIT_RUN && o.effect != ZK_TAP_SUBMIT_STOP, "schedules posted");
    TCHECK(o.effect != ZK_TAP_SUBMIT_PAUSE && o.effect != ZK_TAP_SUBMIT_RESUME, "schedules posted a pause");
    zk_flow_tap(ZK_TARGET_HOME, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_HOME);
    TCHECK(o.effect != ZK_TAP_SUBMIT_RUN && o.effect != ZK_TAP_SUBMIT_STOP, "home posted");
    zk_flow_tap(ZK_TARGET_SCHED_NEXT, &tile, &o);
    TEQ_I((int)o.effect, (int)ZK_TAP_SCHED_NEXT);
    TCHECK(o.effect != ZK_TAP_SUBMIT_RUN && o.effect != ZK_TAP_SUBMIT_STOP, "next posted");
    TEQ_I(zk_flow_debounce_ok(0, 1000), 1);
    TEQ_I(zk_flow_debounce_ok(1000, 1299), 0);
    TEQ_I(zk_flow_debounce_ok(1000, 1300), 1);
}

static void test_offline_stop_ack(void)
{
    zk_stop_flow_t f;
    zk_kiosk_t k;
    zk_view_in_t in;
    zk_view_t v;
    memset(&f, 0, sizeof f);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    TEQ_I(zk_stop_confirm(&f), 1);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(zk_stop_confirm(&f), 0);
    zk_stop_http(&f, 0);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(f.inflight, 0);
    /* Waiting to retry: a confirm sends again. One POST at a time. */
    TEQ_I(zk_stop_confirm(&f), 1);
    TEQ_I(f.inflight, 1);
    zk_stop_http(&f, 0);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    TEQ_I(zk_stop_retry(&f), 1);
    TEQ_I(zk_stop_retry(&f), 0);
    zk_stop_http(&f, 1);
    TEQ_I((int)f.phase, (int)ZK_STOP_WAIT);
    TEQ_I(zk_stop_shows_stopped(&f), 0);

    /* 2xx while water is still on stays pending and the caller posts again. */
    memset(&f, 0, sizeof f);
    TEQ_I(zk_stop_confirm(&f), 1);
    zk_stop_http(&f, 1);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_STILL_ON), 1);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(f.inflight, 1);

    /* 2xx, then the controller is unreachable: stay pending and post again. */
    memset(&f, 0, sizeof f);
    TEQ_I(zk_stop_confirm(&f), 1);
    zk_stop_http(&f, 1);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_UNAVAILABLE), 1);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(f.inflight, 1);

    memset(&f, 0, sizeof f);
    zk_stop_confirm(&f);
    zk_stop_http(&f, 1);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_CLEAR), 0);
    TEQ_I(zk_stop_shows_stopped(&f), 1);
    TEQ_I((int)f.phase, (int)ZK_STOP_CONFIRMED);

    /* All-off while the retry is waiting confirms without another POST. */
    memset(&f, 0, sizeof f);
    zk_stop_confirm(&f);
    zk_stop_http(&f, 0);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_CLEAR), 0);
    TEQ_I(zk_stop_shows_stopped(&f), 1);

    /* Still on while waiting to retry does not confirm. The timer posts. */
    memset(&f, 0, sizeof f);
    zk_stop_confirm(&f);
    zk_stop_http(&f, 0);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_STILL_ON), 0);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(f.inflight, 0);
    TEQ_I(zk_stop_shows_stopped(&f), 0);

    /* The snapshot that arrives with the POST is not a decision. */
    memset(&f, 0, sizeof f);
    zk_stop_confirm(&f);
    TEQ_I(zk_stop_snapshot(&f, ZK_STOP_SNAP_CLEAR), 0);
    TEQ_I((int)f.phase, (int)ZK_STOP_QUEUED);
    TEQ_I(f.inflight, 1);
    TEQ_I(zk_stop_shows_stopped(&f), 0);

    base_yard(&k, 4);
    wall_at(&k.now, 2026, 10, 20, 6, 42);
    memset(&in, 0, sizeof in);
    in.stop_phase = ZK_STOP_QUEUED;
    build(&k, NULL, &in, 1, &v);
    TCHECK(strcmp(v.hero.headline, "Watering stopped") != 0, "queued copy");
    TEQ_S(v.hero.headline, ZK_STOP_PENDING_HEADLINE);
    TEQ_S(v.hero.secondary, ZK_STOP_PENDING_OFFLINE);
    TCHECK(strstr(v.hero.headline, "sent yet") == NULL, "queued still says sent yet");
    in.stop_phase = ZK_STOP_WAIT;
    k.watering = 1;
    k.has_run = 1;
    zk_str_copy(k.run.kind, sizeof k.run.kind, "schedule");
    zk_str_copy(k.run.current_station, sizeof k.run.current_station, "lawn-a");
    build(&k, NULL, &in, 0, &v);
    TCHECK(strcmp(v.hero.headline, "Watering stopped") != 0, "http ok still watering");
    TEQ_S(v.hero.headline, ZK_STOP_PENDING_HEADLINE);
    TEQ_S(v.hero.secondary, ZK_STOP_PENDING_RETRY);
    TEQ_S(v.hero.label, ZK_STOP_PENDING_LABEL);
    TCHECK(strstr(v.hero.label, "NO REPLY") == NULL, "wait still says no reply");
    TEQ_I((int)v.hero.style, (int)ZK_HERO_STOP_PENDING);
    TEQ_I(v.stop_filled, 1);

    in.stop_phase = ZK_STOP_WAIT;
    build(&k, NULL, &in, 1, &v);
    TEQ_S(v.hero.headline, ZK_STOP_PENDING_HEADLINE);
    TEQ_S(v.hero.secondary, ZK_STOP_PENDING_OFFLINE);
    TCHECK(strstr(v.hero.headline, "sent yet") == NULL, "unreachable after 2xx says sent yet");
    TEQ_I((int)v.hero.style, (int)ZK_HERO_STOP_PENDING);
    TEQ_I(v.stop_filled, 1);
    TCHECK(strcmp(v.hero.headline, "Watering stopped") != 0, "unreachable after 2xx is not stopped");
}

static void test_soil_never_zero_and_layout(void)
{
    zk_kiosk_t k;
    zk_schedules_t sc;
    zk_view_in_t in;
    zk_view_t on, off;
    zk_soil_doc_t doc;
    zk_copy_src_t src;
    zk_layout_rects a, b;
    zk_layout_in lin;
    zk_soil_cfg_t cfg;
    int i;

    base_yard(&k, 6);
    next_weekdays(&k, &sc);
    k.soil.enabled = 1;
    k.soil.show_bars = 1;
    memset(&doc, 0, sizeof doc);
    doc.configured = 1;
    doc.enabled = 1;
    zk_str_copy(doc.zones[0].id, ZK_ID_MAX, "lawn-a");
    doc.zones[0].has_percent = 1;
    doc.zones[0].percent = 52;
    doc.zones[0].rate_measured = 1;
    zk_str_copy(doc.zones[1].id, ZK_ID_MAX, "lawn-b");
    doc.zones[1].has_percent = 1;
    doc.zones[1].percent = 22;
    doc.zones[1].rate_measured = 1;
    zk_str_copy(doc.zones[2].id, ZK_ID_MAX, "drip");
    doc.zones[2].rate_measured = 0;
    zk_str_copy(doc.zones[3].id, ZK_ID_MAX, "beds");
    doc.zones[3].has_percent = 1;
    doc.zones[3].percent = 88;
    doc.zones[3].rate_measured = 1;
    zk_str_copy(doc.zones[4].id, ZK_ID_MAX, "trees");
    doc.zones[4].has_percent = 1;
    doc.zones[4].percent = 61;
    doc.zones[4].rate_measured = 1;
    doc.zones[4].has_age = 1;
    doc.zones[4].age_days = 2;
    zk_str_copy(doc.zones[5].id, ZK_ID_MAX, "side");
    doc.zones[5].has_percent = 1;
    doc.zones[5].percent = 45;
    doc.zones[5].rate_measured = 1;
    doc.n_zones = 6;
    zk_soil_cfg_defaults(&cfg);
    memset(&in, 0, sizeof in);
    in.soil_cfg = &cfg;
    memset(&src, 0, sizeof src);
    src.kiosk = &k;
    src.have_kiosk = 1;
    src.schedules = &sc;
    src.have_schedules = 1;
    src.wall = k.now;
    src.soil_doc = &doc;
    zk_view_build(&src, &in, &on);
    TEQ_S(on.tiles[0].slot, "52% " ZK_MID " OK");
    TEQ_S(on.tiles[1].slot, "22% " ZK_MID " Dry");
    TEQ_S(on.tiles[2].slot, "Rate not measured");
    TCHECK(strstr(on.tiles[2].slot, "0%") == NULL, "missing is not zero percent");
    TEQ_I(on.tiles[2].percent, -1);
    TEQ_S(on.tiles[3].slot, "88% " ZK_MID " Wet");
    TEQ_S(on.tiles[4].slot, "61% " ZK_MID " 2d old");
    TEQ_S(on.tiles[5].slot, "45% " ZK_MID " OK");

    /* A null percent with the feature on is "Rate not measured", never 0%. */
    doc.zones[0].has_percent = 0;
    doc.zones[0].percent = 0;
    zk_view_build(&src, &in, &on);
    TEQ_S(on.tiles[0].slot, "Rate not measured");
    TCHECK(strcmp(on.tiles[0].slot, "0% " ZK_MID " Dry") != 0, "null percent");

    /* A real measured 0 is allowed to say 0%. */
    doc.zones[0].has_percent = 1;
    doc.zones[0].percent = 0;
    doc.zones[0].rate_measured = 1;
    zk_view_build(&src, &in, &on);
    TEQ_S(on.tiles[0].slot, "0% " ZK_MID " Dry");

    /* Feature off keeps the same layout rects. */
    src.soil_doc = NULL;
    k.soil.enabled = 0;
    zk_view_build(&src, &in, &off);
    TEQ_I(on.n_tiles, off.n_tiles);
    memset(&lin, 0, sizeof lin);
    lin.n_stations = 6;
    zk_layout_rects_of(ZK_SCREEN_HOME, &lin, &a);
    zk_layout_rects_of(ZK_SCREEN_HOME, &lin, &b);
    TCHECK(memcmp(&a, &b, sizeof a) == 0, "rects identical");
    for (i = 0; i < 6; i++) {
        TEQ_I(a.slots[i].w, 156);
        TEQ_I(a.slots[i].h, 24);
    }
    lin.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_HOME, &lin, &a);
    lin.n_stations = 4;
    zk_layout_rects_of(ZK_SCREEN_HOME, &lin, &b);
    TCHECK(memcmp(&a.tiles, &b.tiles, sizeof a.tiles) == 0, "4-up tiles");
    TCHECK(memcmp(&a.slots, &b.slots, sizeof a.slots) == 0, "4-up slots");

    /* Global age from updated_at, not a per-zone hint. */
    memset(&doc, 0, sizeof doc);
    doc.configured = 1;
    doc.enabled = 1;
    doc.et_stale = 1;
    zk_str_copy(doc.zones[0].id, ZK_ID_MAX, "lawn-a");
    doc.zones[0].has_percent = 1;
    doc.zones[0].percent = 40;
    doc.zones[0].rate_measured = 1;
    doc.n_zones = 1;
    k.soil.enabled = 1;
    src.soil_doc = &doc;
    zk_view_build(&src, &in, &on);
    TEQ_S(on.tiles[0].slot, "40% " ZK_MID " 2d old");
}

static void expect_line(zk_repaint_event_t ev, const zk_repaint_ctx_t *ctx, const char *want)
{
    char buf[512];
    int n = zk_repaint_format(ev, ctx, buf, sizeof buf);
    TCHECK(n > 0, "format %s", zk_repaint_name(ev));
    TEQ_S(buf, want);
}

static void add_step(zk_schedule_t *s, const char *id, int minutes)
{
    if (s->n_steps >= ZK_MAX_STEPS) {
        return;
    }
    zk_str_copy(s->steps[s->n_steps].station_id, ZK_ID_MAX, id);
    s->steps[s->n_steps].minutes = minutes;
    s->n_steps++;
}

static void test_schedules_copy(void)
{
    zk_kiosk_t k;
    zk_schedules_t sc;
    zk_view_in_t in;
    zk_view_t v;
    zk_copy_src_t src;
    zk_schedule_t *a;
    zk_schedule_t *b;
    base_yard(&k, 4);
    memset(&sc, 0, sizeof sc);
    a = &sc.items[sc.n++];
    zk_str_copy(a->id, sizeof a->id, "lawn-morning");
    zk_str_copy(a->name, sizeof a->name, "Lawn " ZK_MID " Morning");
    a->enabled = 1;
    a->has_start = 1;
    a->start_hh = 6;
    a->start_mm = 10;
    a->weekdays = ZK_WD_ALL;
    add_step(a, "lawn-a", 6);
    add_step(a, "lawn-b", 6);
    add_step(a, "beds", 6);
    b = &sc.items[sc.n++];
    zk_str_copy(b->id, sizeof b->id, "beds-2d");
    zk_str_copy(b->name, sizeof b->name, "Beds " ZK_MID " Every 2 days");
    b->enabled = 0;
    b->has_start = 1;
    b->start_hh = 7;
    b->start_mm = 0;
    b->weekdays = ZK_WD_MON | ZK_WD_WED | ZK_WD_FRI;
    add_step(b, "beds", 8);
    add_step(b, "drip", 4);
    memset(&in, 0, sizeof in);
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sched.label, ZK_SCHED_READ);
    TEQ_I(v.sched.n_rows, 2);
    TEQ_I(v.sched.n_total, 2);
    TEQ_I(v.sched.next_on, 0);
    TEQ_S(v.sched.rows[0].name, "Lawn " ZK_MID " Morning");
    TEQ_I(v.sched.rows[0].enabled, 1);
    TEQ_S(v.sched.rows[0].meta,
          "Every day " ZK_MID " 6:10" ZK_NBSP "AM " ZK_MID " 3 steps " ZK_MID " 18 min " ZK_MID " Enabled");
    TEQ_S(v.sched.rows[1].name, "Beds " ZK_MID " Every 2 days");
    TEQ_I(v.sched.rows[1].enabled, 0);
    TEQ_S(v.sched.rows[1].meta,
          "Mon Wed Fri " ZK_MID " 7:00" ZK_NBSP "AM " ZK_MID " 2 steps " ZK_MID " 12 min " ZK_MID " Disabled");
    TEQ_S(v.sched.note, ZK_SCHED_NOTE);
    TEQ_S(v.sched.empty_title, ZK_SCHED_EMPTY);
    TEQ_S(v.sched.empty_body, ZK_SCHED_EMPTY_BODY);

    in.sched_index = 1;
    build(&k, &sc, &in, 0, &v);
    TEQ_I(v.sched.n_rows, 1);
    TEQ_S(v.sched.rows[0].name, "Beds " ZK_MID " Every 2 days");
    TEQ_I(v.sched.rows[1].on, 0);
    TEQ_I(v.sched.next_on, 0);

    {
        zk_schedule_t *c = &sc.items[sc.n++];
        zk_str_copy(c->id, sizeof c->id, "trees-sun");
        zk_str_copy(c->name, sizeof c->name, "Trees " ZK_MID " Sundays");
        c->enabled = 1;
        c->has_start = 1;
        c->start_hh = 6;
        c->start_mm = 10;
        c->weekdays = ZK_WD_SUN;
        add_step(c, "trees", 6);
        in.sched_index = 0;
        build(&k, &sc, &in, 0, &v);
        TEQ_I(v.sched.next_on, 1);
        TEQ_I(v.sched.n_rows, 2);
        TEQ_S(v.sched.label, "SCHEDULES " ZK_MID " READ ONLY " ZK_MID " 1" ZK_EN "2 of 3");
        in.sched_index = 2;
        build(&k, &sc, &in, 0, &v);
        TEQ_I(v.sched.n_rows, 1);
        TEQ_S(v.sched.rows[0].name, "Trees " ZK_MID " Sundays");
        TEQ_S(v.sched.label, "SCHEDULES " ZK_MID " READ ONLY " ZK_MID " 3 of 3");
        TEQ_I(v.sched.next_on, 1);
        build(&k, &sc, &in, 1, &v);
        TEQ_S(v.sched.label, "SCHEDULES " ZK_MID " LAST UPDATE 6:00" ZK_NBSP "PM " ZK_MID " 3 of 3");
        in.sched_index = 0;
        sc.n = 2;
    }

    a->weekdays = ZK_WD_MON | ZK_WD_TUE | ZK_WD_WED | ZK_WD_THU | ZK_WD_FRI;
    a->n_steps = 1;
    a->steps[0].minutes = 6;
    in.sched_index = 0;
    sc.n = 1;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sched.rows[0].meta, "Weekdays " ZK_MID " 6:10" ZK_NBSP "AM " ZK_MID " 1 step " ZK_MID " 6 min " ZK_MID " Enabled");
    TEQ_I(v.sched.next_on, 0);

    a->weekdays = ZK_WD_SAT | ZK_WD_SUN;
    a->has_start = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sched.rows[0].meta, "Weekends " ZK_MID " No start " ZK_MID " 1 step " ZK_MID " 6 min " ZK_MID " Enabled");

    a->name[0] = 0;
    zk_str_copy(a->note, sizeof a->note, "From note");
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sched.rows[0].name, "From note");
    a->note[0] = 0;
    build(&k, &sc, &in, 0, &v);
    TEQ_S(v.sched.rows[0].name, "lawn-morning");

    memset(&src, 0, sizeof src);
    memset(&sc, 0, sizeof sc);
    memset(&in, 0, sizeof in);
    src.kiosk = &k;
    src.have_kiosk = 1;
    src.schedules = &sc;
    src.have_schedules = 1;
    src.wall = k.now;
    zk_view_build(&src, &in, &v);
    TEQ_I(v.sched.n_rows, 0);
    TEQ_I(v.sched.next_on, 0);
    TEQ_S(v.sched.label, ZK_SCHED_READ);
    TEQ_S(v.sched.empty_title, ZK_SCHED_EMPTY);
    src.unreachable = 1;
    zk_view_build(&src, &in, &v);
    TEQ_S(v.sched.label, "SCHEDULES " ZK_MID " LAST UPDATE 6:00" ZK_NBSP "PM");
}

static void test_idle_home(void)
{
    zk_stop_flow_t f;
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_HOME, 60000), 0);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_HOME, 120000), 0);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SCHEDULES, 59999), 0);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SCHEDULES, 60000), 1);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_RUN, 60000), 1);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_ANYWAY, 60000), 1);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_STOP_STATION, 60000), 1);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_OVERLAP, 60000), 1);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_STOP_ALL, 59999), 0);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_STOP_ALL, 60000), 1);
    memset(&f, 0, sizeof f);
    f.phase = ZK_STOP_WAIT;
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_STOP_ALL, 60000), 1);
    TEQ_I((int)f.phase, (int)ZK_STOP_WAIT);
    TEQ_I(zk_stop_shows_stopped(&f), 0);
    /* The pure predicate stays on 60000. The UI reads ZK_IDLE_HOME_MS. */
    setenv("ZK_IDLE_HOME_MS", "1500", 1);
    TEQ_I((int)zk_idle_home_ms(), 1500);
    TEQ_I(zk_flow_idle_returns_home(ZK_SCREEN_SHEET_STOP_ALL, 1500), 0);
    TEQ_I(zk_flow_idle_returns_home_limit(ZK_SCREEN_SHEET_STOP_ALL, 1499, zk_idle_home_ms()), 0);
    TEQ_I(zk_flow_idle_returns_home_limit(ZK_SCREEN_SHEET_STOP_ALL, 1500, zk_idle_home_ms()), 1);
    TEQ_I(zk_flow_idle_returns_home_limit(ZK_SCREEN_HOME, 1500, 1500), 0);
    setenv("ZK_IDLE_HOME_MS", "nope", 1);
    TEQ_I((int)zk_idle_home_ms(), 60000);
    setenv("ZK_IDLE_HOME_MS", "0", 1);
    TEQ_I((int)zk_idle_home_ms(), 60000);
    setenv("ZK_IDLE_HOME_MS", "600001", 1);
    TEQ_I((int)zk_idle_home_ms(), 60000);
    unsetenv("ZK_IDLE_HOME_MS");
    TEQ_I((int)zk_idle_home_ms(), 60000);
    TEQ_I(zk_flow_idle_returns_home_limit(ZK_SCREEN_SHEET_STOP_ALL, 59999, 0), 0);
    TEQ_I(zk_flow_idle_returns_home_limit(ZK_SCREEN_SHEET_STOP_ALL, 60000, 0), 1);
}

static void test_dirty_rects(void)
{
    zk_repaint_ctx_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.tile_a = -1;
    ctx.tile_b = -1;
    ctx.n_stations = 6;
    expect_line(ZK_EVT_BOOT, &ctx, "dirty event=boot rects=0,0,800x480\n");
    expect_line(ZK_EVT_RETURN_ONLINE, &ctx, "dirty event=return_online rects=0,0,800x480\n");
    expect_line(ZK_EVT_CLOSE_FULL_SHEET, &ctx, "dirty event=close_full_sheet rects=0,0,800x480\n");
    expect_line(ZK_EVT_OPEN_STOP_ALL, &ctx, "dirty event=open_stop_all rects=0,0,800x480\n");
    expect_line(ZK_EVT_OFFLINE_CONFIRM, &ctx, "dirty event=offline_confirm rects=0,0,800x480\n");
    ctx.tile_a = 2;
    expect_line(ZK_EVT_MINUTE_TICK, &ctx, "dirty event=minute_tick rects=40,215,328x77;404,130,184x102\n");
    ctx.tile_b = 3;
    expect_line(ZK_EVT_NEXT_STATION, &ctx,
                "dirty event=next_station rects=16,16,376x332;404,130,184x102;600,130,184x102\n");
    expect_line(ZK_EVT_IDLE_WATER, &ctx, "dirty event=idle_water rects=16,16,376x332;404,130,184x102;404,360,380x104\n");
    expect_line(ZK_EVT_MANUAL, &ctx, "dirty event=manual rects=16,16,376x332;404,130,184x102;404,360,380x104\n");
    expect_line(ZK_EVT_PAUSE_RESUME, &ctx, "dirty event=pause_resume rects=16,16,376x332;148,360,244x104\n");
    expect_line(ZK_EVT_OVERLAP, &ctx, "dirty event=overlap rects=16,16,376x332\n");
    /* Run, anyway, running tile, and overlap open and close share this rect. */
    expect_line(ZK_EVT_SHEET, &ctx, "dirty event=sheet rects=16,16,768x332\n");
    ctx.stepper_cross = 1;
    expect_line(ZK_EVT_STEPPER, &ctx, "dirty event=stepper rects=144,232,132x96;36,62,728x80;36,232,96x96\n");
    ctx.stepper_cross = 0;
    expect_line(ZK_EVT_STEPPER, &ctx, "dirty event=stepper rects=144,232,132x96;36,62,728x80\n");
    expect_line(ZK_EVT_CONTROLLER_LOST, &ctx,
                "dirty event=controller_lost rects=16,16,376x332;404,16,184x102;600,16,184x102;404,130,184x102;"
                "600,130,184x102;404,244,184x102;600,244,184x102;148,360,244x104;16,360,120x104\n");
    ctx.soil_mask = 1u << 4;
    ctx.rain = 1;
    expect_line(ZK_EVT_SOIL_RAIN, &ctx, "dirty event=soil_rain rects=418,297,156x24;40,264,328x64\n");
    expect_line(ZK_EVT_RETRY_TICK, &ctx, "dirty event=retry_tick rects=38,34,332x44\n");
    expect_line(ZK_EVT_ACK, &ctx,
                "dirty event=ack rects=16,16,376x332;404,16,184x102;600,16,184x102;404,130,184x102;600,130,184x102;"
                "404,244,184x102;600,244,184x102;148,360,244x104;404,360,380x104\n");
    expect_line(ZK_EVT_SCHEDULES, &ctx, "dirty event=schedules rects=16,16,768x332;16,360,120x104\n");

    /* 4-up running tile is the tall cell. */
    ctx.n_stations = 4;
    ctx.tile_a = 0;
    ctx.soil_mask = 0;
    ctx.rain = 0;
    expect_line(ZK_EVT_MINUTE_TICK, &ctx, "dirty event=minute_tick rects=40,215,328x77;404,16,184x160\n");
}

int main(void)
{
    test_copy_states();
    test_copy_sheets();
    test_label_clamp_headline_intact();
    test_tile_tap_never_runs();
    test_schedules_copy();
    test_offline_stop_ack();
    test_idle_home();
    test_soil_never_zero_and_layout();
    test_dirty_rects();
    return zk_test_report("test_flow");
}
