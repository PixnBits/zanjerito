#include "zk_test.h"
#include "zk_logic.h"
#include "zk_model.h"

static void check_common_stations(const zk_stations_t *st)
{
    TEQ_I(st->n, 4);
    TEQ_S(st->items[0].id, "az01");
    TEQ_S(st->items[0].title, "Test Station 1");
    TEQ_S(st->items[0].color, "red");
    TEQ_S(st->items[3].id, "az04");
    TEQ_S(st->items[2].title, "Test Station 3");
}

static void check_common_schedules(const zk_schedules_t *sc)
{
    TEQ_I(sc->n, 2);
    TEQ_S(sc->items[0].id, "morning");
    TEQ_I(sc->items[0].enabled, 1);
    TEQ_I(sc->items[0].has_start, 1);
    TEQ_I(sc->items[0].start_hh, 8);
    TEQ_I(sc->items[0].start_mm, 23);
    TEQ_I(sc->items[0].n_steps, 3);
    TEQ_S(sc->items[0].steps[0].station_id, "az01");
    TEQ_I(sc->items[0].steps[0].minutes, 2);
    TEQ_S(sc->items[0].steps[1].station_id, "az02");
    TEQ_I(sc->items[0].steps[1].minutes, 3);
    TEQ_S(sc->items[0].steps[2].station_id, "az04");
    TEQ_I(sc->items[0].steps[2].minutes, 3);
    TEQ_I(sc->items[0].weekdays, ZK_WD_ALL);
    TEQ_I(sc->items[1].enabled, 0);
    TEQ_S(sc->items[1].id, "drip-weekly");
    TEQ_I(sc->items[1].has_starts_on, 1);
    TEQ_I(sc->items[1].starts_y, 2026);
    TEQ_I(sc->items[1].starts_m, 9);
    TEQ_I(sc->items[1].starts_d, 16);
    TEQ_I(sc->items[1].has_ends_on, 1);
    TEQ_I(sc->items[1].ends_d, 29);
    TEQ_I(sc->items[1].weekdays, ZK_WD_THU);
    TEQ_I(sc->items[1].n_steps, 1);
}

static void parse_scenario(const char *name)
{
    char *status = zk_fixture(name, "status.json");
    char *stations = zk_fixture(name, "stations.json");
    char *schedules = zk_fixture(name, "schedules.json");
    char *soil = zk_fixture(name, "soil.json");
    zk_status_t st;
    zk_stations_t sta;
    zk_schedules_t sch;
    zk_soil_t so;
    TCHECK(status && stations && schedules && soil, "load %s", name);
    if (!status || !stations || !schedules || !soil) {
        free(status);
        free(stations);
        free(schedules);
        free(soil);
        return;
    }
    TEQ_I(zk_parse_status(status, &st), ZK_OK);
    TEQ_I(zk_parse_stations(stations, &sta), ZK_OK);
    TEQ_I(zk_parse_schedules(schedules, &sch), ZK_OK);
    TEQ_I(zk_parse_soil(soil, &so), ZK_OK);
    TEQ_S(st.timezone, "UTC");
    TEQ_I(st.has_now, 1);
    TEQ_I(st.now.y, 2026);
    TEQ_I(st.now.m, 9);
    TEQ_I(st.now.d, 29);
    TEQ_I(st.now.wday, 2); /* Tuesday */
    check_common_stations(&sta);
    check_common_schedules(&sch);

    if (strcmp(name, "home-rain") == 0) {
        TEQ_S(st.phase, "Idle");
        TEQ_I(st.watering, 0);
        TEQ_I(st.fault, 0);
        TEQ_I(st.paused, 0);
        TEQ_I(st.rain.enabled, 1);
        TEQ_I(st.rain.unavailable, 0);
        TEQ_I(st.rain.have_totals, 1);
        TEQ_D(st.rain.total_72h, 0.24);
        TEQ_I(so.enabled, 1);
        TEQ_I(so.et_known, 1);
        TEQ_I(so.et_stale, 0);
        TEQ_I(so.n_zones, 4);
        TEQ_I(so.zones[0].percent, 58);
        TEQ_I(so.zones[2].percent, 34);
        TEQ_I(so.zones[2].rate_measured, 1);
    } else if (strcmp(name, "home-norain") == 0) {
        TEQ_D(st.rain.total_72h, 0.03);
        TEQ_I(st.rain.have_totals, 1);
    } else if (strcmp(name, "home-nosoil") == 0) {
        TEQ_I(so.enabled, 0);
        TEQ_I(so.et_known, 0);
        TEQ_I(so.n_zones, 0);
    } else if (strcmp(name, "home-stale") == 0) {
        TEQ_I(so.enabled, 1);
        TEQ_I(so.et_stale, 1);
        TEQ_I(so.zones[0].percent, 58);
    } else if (strcmp(name, "home-fault") == 0) {
        TEQ_S(st.phase, "Fault");
        TEQ_I(st.fault, 1);
        TEQ_I(st.watering, 0);
        TEQ_I(st.lockout, 1);
        TCHECK(strstr(st.last_error, "valve") != NULL, "last_error %s", st.last_error);
    } else if (strcmp(name, "running") == 0) {
        TEQ_S(st.phase, "StationOn");
        TEQ_I(st.watering, 1);
        TEQ_I(st.fault, 0);
        TEQ_S(st.current_station, "az02");
        TEQ_I(st.n_on, 1);
        TEQ_S(st.stations_on[0], "az02");
        TEQ_I(st.now.hh, 8);
        TEQ_I(st.now.mm, 25);
        TEQ_I(st.now.ss, 19);
    } else if (strcmp(name, "paused-rain") == 0) {
        TEQ_I(st.paused, 1);
        TEQ_S(st.pause_source, "auto");
        TEQ_S(st.reason, "rain");
        TEQ_I(st.has_paused_until, 1);
        TEQ_I(st.paused_until.y, 2026);
        TEQ_I(st.paused_until.m, 9);
        TEQ_I(st.paused_until.d, 30);
        TEQ_I(st.paused_until.hh, 6);
        TEQ_I(st.paused_until.wday, 3); /* Wed */
    } else if (strcmp(name, "paused-manual") == 0) {
        TEQ_I(st.paused, 1);
        TEQ_S(st.pause_source, "manual");
        TEQ_I(st.has_paused_until, 0);
        TEQ_S(st.reason, "rain");
    }

    free(status);
    free(stations);
    free(schedules);
    free(soil);
}

static void test_garbage(void)
{
    zk_status_t st;
    zk_stations_t sta;
    zk_schedules_t sch;
    zk_soil_t so;
    TEQ_I(zk_parse_status(NULL, &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("{", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("null", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("[]", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("{]", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status("{\"phase\":", &st), ZK_ERR_PARSE);
    TEQ_I(zk_parse_status(NULL, NULL), ZK_ERR_ARG);
    TEQ_I(zk_parse_stations("not json", &sta), ZK_ERR_PARSE);
    TEQ_I(zk_parse_schedules("", &sch), ZK_ERR_PARSE);
    TEQ_I(zk_parse_soil("[1,2,3]", &so), ZK_ERR_PARSE);
}

static void test_defaults(void)
{
    zk_status_t st;
    zk_stations_t sta;
    zk_schedules_t sch;
    zk_soil_t so;
    TEQ_I(zk_parse_status("{}", &st), ZK_OK);
    TEQ_S(st.phase, "");
    TEQ_I(st.watering, 0);
    TEQ_I(st.fault, 0);
    TEQ_I(st.paused, 0);
    TEQ_I(st.has_paused_until, 0);
    TEQ_I(st.rain.have_totals, 0);
    TEQ_I(zk_parse_status("{\"phase\":null,\"paused_until\":null,\"stations_on\":null,\"rain\":{}}", &st), ZK_OK);
    TEQ_S(st.phase, "");
    TEQ_I(st.has_paused_until, 0);
    TEQ_I(st.n_on, 0);
    TEQ_I(st.rain.enabled, 0);
    TEQ_I(zk_parse_status("{\"phase\":1,\"lockout\":\"yes\",\"now\":123}", &st), ZK_OK);
    TEQ_S(st.phase, "");
    TEQ_I(st.lockout, 0);
    TEQ_I(st.has_now, 0);
    TEQ_I(zk_parse_stations("{}", &sta), ZK_OK);
    TEQ_I(sta.n, 0);
    TEQ_I(zk_parse_stations("{\"stations\":[null,{\"id\":\"az01\"},\"x\",{}]}", &sta), ZK_OK);
    TEQ_I(sta.n, 1);
    TEQ_S(sta.items[0].id, "az01");
    TEQ_I(zk_parse_schedules("{\"schedules\":[{\"id\":\"x\",\"enabled\":true,\"start\":\"25:99\",\"weekdays\":[\"nope\"]}]}", &sch), ZK_OK);
    TEQ_I(sch.n, 1);
    TEQ_I(sch.items[0].has_start, 0);
    TEQ_I(sch.items[0].weekdays, 0);
    TEQ_I(zk_parse_soil("{\"enabled\":true,\"zones\":[{\"station_id\":\"az01\",\"percent\":null}]}", &so), ZK_OK);
    TEQ_I(so.n_zones, 1);
    TEQ_I(so.zones[0].percent, -1);
}

static void test_huge_rain(void)
{
    zk_status_t st;
    TEQ_I(zk_parse_status("{\"phase\":\"Idle\",\"rain\":{\"enabled\":true,\"unavailable\":false,\"total_72h_inches\":1e999,\"total_24h_inches\":0.2}}", &st), ZK_OK);
    TEQ_I(st.rain.enabled, 1);
    TEQ_I(st.rain.have_totals, 0);
    TEQ_I(zk_rain_strip_visible(&st), 0);
    TEQ_I(zk_parse_status("{\"rain\":{\"enabled\":true,\"total_72h_inches\":0.2,\"total_24h_inches\":1e999}}", &st), ZK_OK);
    TEQ_I(st.rain.have_totals, 0);
    TEQ_I(zk_rain_strip_visible(&st), 0);
    TEQ_I(zk_parse_status("{\"rain\":{\"enabled\":true,\"total_72h_inches\":1e30}}", &st), ZK_OK);
    TEQ_I(st.rain.have_totals, 0);
    TEQ_I(zk_parse_status("{\"rain\":{\"enabled\":true,\"total_72h_inches\":1000}}", &st), ZK_OK);
    TEQ_I(st.rain.have_totals, 1);
    TEQ_I(zk_parse_status("{\"rain\":{\"enabled\":true,\"total_72h_inches\":1000.1}}", &st), ZK_OK);
    TEQ_I(st.rain.have_totals, 0);
}

static void test_wall_parse(void)
{
    zk_wall_t w, w2;
    TEQ_I(zk_parse_wall("2026-09-29T06:52:00-06:00", &w), ZK_OK);
    TEQ_I(w.y, 2026);
    TEQ_I(w.hh, 6);
    TEQ_I(w.mm, 52);
    TEQ_I(w.ss, 0);
    TEQ_I(w.wday, 2);
    TEQ_I(zk_parse_wall("2026-09-29T06:52:00+00:00", &w2), ZK_OK);
    TEQ_I(w2.hh, 6);
    TEQ_I(w2.wday, 2);
    TEQ_I(zk_parse_wall("2026-09-29T06:52:00.123Z", &w), ZK_OK);
    TEQ_I(w.ss, 0);
    TEQ_I(zk_parse_wall("not-a-date", &w), ZK_ERR_PARSE);
}

static void test_wall_advance(void)
{
    zk_wall_t w;
    zk_parse_wall("2026-09-29T06:52:00-06:00", &w);
    w = zk_wall_advance(w, 10100, 100);
    TEQ_I(w.ss, 10);
    TEQ_I(w.mm, 52);
    zk_parse_wall("2026-12-31T23:59:50-07:00", &w);
    w = zk_wall_advance(w, 15000, 0);
    TEQ_I(w.y, 2027);
    TEQ_I(w.m, 1);
    TEQ_I(w.d, 1);
    TEQ_I(w.hh, 0);
    TEQ_I(w.mm, 0);
    TEQ_I(w.ss, 5);
    TEQ_I(w.wday, 5); /* Friday */
}

int main(void)
{
    const char *scen[] = {
        "home-rain", "home-norain", "home-nosoil", "home-stale",
        "home-fault", "running", "paused-rain", "paused-manual"
    };
    size_t i;
    for (i = 0; i < sizeof(scen) / sizeof(scen[0]); i++) {
        parse_scenario(scen[i]);
    }
    test_garbage();
    test_defaults();
    test_huge_rain();
    test_wall_parse();
    test_wall_advance();
    return zk_test_report("test_json");
}
