#include "zk_test.h"
#include "zk_console.h"
#include "zk_logic.h"
#include "zk_model.h"

#include <fcntl.h>
#include <unistd.h>

static void check_common_stations(const zk_kiosk_t *k)
{
    TEQ_I(k->n_stations, 4);
    TEQ_S(k->stations[0].id, "az01");
    TEQ_S(k->stations[0].title, "Test Station 1");
    TEQ_S(k->stations[0].color, "red");
    TEQ_S(k->stations[3].id, "az04");
    TEQ_S(k->stations[2].title, "Test Station 3");
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
    char *kiosk = zk_fixture(name, "kiosk.json");
    char *schedules = zk_fixture(name, "schedules.json");
    zk_kiosk_t k;
    zk_schedules_t sch;
    TCHECK(kiosk && schedules, "load %s", name);
    if (!kiosk || !schedules) {
        free(kiosk);
        free(schedules);
        return;
    }
    TEQ_I(zk_parse_kiosk(kiosk, &k), ZK_OK);
    TEQ_I(zk_parse_schedules(schedules, &sch), ZK_OK);
    TEQ_S(k.timezone, "America/Denver");
    TEQ_I(k.has_now, 1);
    TEQ_I(k.now.y, 2026);
    TEQ_I(k.now.m, 9);
    TEQ_I(k.now.d, 29);
    TEQ_I(k.now.wday, 2); /* Tuesday */
    if (strcmp(name, "home-longnames") != 0 && strcmp(name, "running-long") != 0) {
        check_common_stations(&k);
    }
    check_common_schedules(&sch);

    if (strcmp(name, "home-rain") == 0) {
        TEQ_S(k.phase, "Idle");
        TEQ_I(k.watering, 0);
        TEQ_I(k.fault, 0);
        TEQ_I(k.pause.paused, 0);
        TEQ_I(k.rain.enabled, 1);
        TEQ_I(k.rain.unavailable, 0);
        TEQ_I(k.rain.have_totals, 1);
        TEQ_D(k.rain.total_72h, 0.24);
        TEQ_I(k.rain_strip.show, 1);
        TEQ_I(zk_rain_strip_visible(&k), 1);
        TEQ_I(k.soil.enabled, 1);
        TEQ_I(k.soil.et_known, 1);
        TEQ_I(k.soil.et_stale, 0);
        TEQ_I(k.soil.show_bars, 1);
        TEQ_I(k.stations[0].soil_percent, 58);
        TEQ_I(k.stations[2].soil_percent, 34);
        TEQ_I(k.stations[1].rain_pause_exempt, 1);
        TEQ_I(zk_station_soil_percent(&k, 0), 58);
    } else if (strcmp(name, "home-norain") == 0) {
        TEQ_D(k.rain.total_72h, 0.03);
        TEQ_I(k.rain.have_totals, 1);
        TEQ_I(k.rain_strip.show, 0);
        TEQ_I(zk_rain_strip_visible(&k), 0);
    } else if (strcmp(name, "home-nosoil") == 0) {
        TEQ_I(k.soil.enabled, 0);
        TEQ_I(k.soil.et_known, 0);
        TEQ_I(k.soil.show_bars, 0);
        TEQ_I(k.stations[0].soil_percent, -1);
        TEQ_I(zk_station_soil_percent(&k, 0), -1);
    } else if (strcmp(name, "home-stale") == 0) {
        TEQ_I(k.soil.enabled, 1);
        TEQ_I(k.soil.et_stale, 1);
        TEQ_I(k.soil.show_bars, 0);
        TEQ_I(zk_station_soil_percent(&k, 0), -1);
    } else if (strcmp(name, "home-fault") == 0) {
        TEQ_S(k.phase, "Fault");
        TEQ_I(k.fault, 1);
        TEQ_I(k.watering, 0);
        TEQ_I(k.lockout, 1);
        TCHECK(strstr(k.last_error, "valve") != NULL, "last_error %s", k.last_error);
    } else if (strcmp(name, "running") == 0) {
        TEQ_S(k.phase, "StationOn");
        TEQ_I(k.has_run, 1);
        TEQ_I(k.watering, 1);
        TEQ_I(k.fault, 0);
        TEQ_S(k.current_station, "az02");
        TEQ_I(k.n_on, 1);
        TEQ_S(k.stations_on[0], "az02");
        TEQ_I(k.now.hh, 8);
        TEQ_I(k.now.mm, 25);
        TEQ_I(k.now.ss, 19);
        TEQ_I(k.run.step_index, 1);
        TEQ_I(k.run.step_count, 3);
        TEQ_I(k.run.step_remaining_sec, 161);
        TEQ_S(k.run.current_station, "az02");
        TEQ_S(k.run.next_station, "az04");
    } else if (strcmp(name, "paused-rain") == 0) {
        TEQ_I(k.pause.paused, 1);
        TEQ_S(k.pause.source, "auto");
        TEQ_S(k.pause.reason, "rain");
        TEQ_I(k.pause.has_until, 1);
        TEQ_I(k.pause.until.y, 2026);
        TEQ_I(k.pause.until.m, 9);
        TEQ_I(k.pause.until.d, 30);
        TEQ_I(k.pause.until.hh, 6);
        TEQ_I(k.pause.until.wday, 3); /* Wed */
        TEQ_I(k.has_next_run, 1);
        TEQ_I(k.next_run.skipped_by_pause, 1);
        TEQ_I(k.has_next_effective, 1);
        TEQ_I(k.next_effective.skipped_by_pause, 0);
        TEQ_I(k.rain_strip.show, 0);
    } else if (strcmp(name, "paused-manual") == 0) {
        TEQ_I(k.pause.paused, 1);
        TEQ_S(k.pause.source, "manual");
        TEQ_I(k.pause.has_until, 0);
        TEQ_I(k.has_next_effective, 0);
        TEQ_I(k.next_run.skipped_by_pause, 1);
    }

    free(kiosk);
    free(schedules);
}

static const char *k_full =
    "{"
    "\"now\":\"2026-09-29T06:52:00-06:00\","
    "\"timezone\":\"America/Denver\","
    "\"phase\":\"Idle\","
    "\"lockout\":false,"
    "\"last_error\":\"\","
    "\"current_station\":\"\","
    "\"stations_on\":[],"
    "\"pause\":{\"paused\":false,\"until\":null,\"label\":\"\",\"reason\":\"\",\"source\":\"\",\"rain_inches\":0},"
    "\"rain_strip\":{\"show\":true,\"inches\":0.049,\"hours\":24},"
    "\"rain\":{\"enabled\":true,\"unavailable\":false,\"total_24h_inches\":0.049,\"total_72h_inches\":0.049,\"have_totals\":true},"
    "\"next_run\":{\"schedule_id\":\"morning\",\"name\":\"Morning cycle\",\"at\":\"2026-09-29T08:23:00-06:00\","
    "\"ends_at\":\"2026-09-29T08:31:00-06:00\",\"total_min\":8,\"skipped_by_pause\":false},"
    "\"next_effective_run\":null,"
    "\"run\":null,"
    "\"stations\":[{\"id\":\"az01\",\"title\":\"Test Station 1\",\"color\":\"red\",\"on\":false,\"state\":\"idle\","
    "\"rain_pause_exempt\":false,\"soil_percent\":58}],"
    "\"soil\":{\"enabled\":true,\"et_known\":true,\"et_stale\":false,\"show_bars\":true,\"updated_at\":null}"
    "}";

static void test_full_and_nulls(void)
{
    zk_kiosk_t k;
    TEQ_I(zk_parse_kiosk(k_full, &k), ZK_OK);
    TEQ_I(k.has_now, 1);
    TEQ_I(k.has_next_run, 1);
    TEQ_I(k.has_next_effective, 0);
    TEQ_I(k.has_run, 0);
    TEQ_I(k.rain_strip.show, 1);
    TEQ_I(zk_rain_strip_visible(&k), 1);
    TEQ_I(k.n_stations, 1);
    TEQ_I(k.stations[0].soil_percent, 58);
    TEQ_I(zk_station_soil_percent(&k, 0), 58);
}

static void test_show_vs_totals(void)
{
    zk_kiosk_t k;
    char json[2048];
    snprintf(json, sizeof json,
             "{\"now\":\"2026-09-29T06:52:00-06:00\",\"timezone\":\"America/Denver\",\"phase\":\"Idle\","
             "\"lockout\":false,\"rain_strip\":{\"show\":true,\"inches\":0.049,\"hours\":24},"
             "\"rain\":{\"enabled\":true,\"unavailable\":false,\"total_24h_inches\":0.049,"
             "\"total_72h_inches\":0.049,\"have_totals\":true},\"stations\":[],\"soil\":{}}");
    TEQ_I(zk_parse_kiosk(json, &k), ZK_OK);
    TEQ_I(zk_rain_strip_visible(&k), 1);
    snprintf(json, sizeof json,
             "{\"now\":\"2026-09-29T06:52:00-06:00\",\"timezone\":\"America/Denver\",\"phase\":\"Idle\","
             "\"lockout\":false,\"rain_strip\":{\"show\":false,\"inches\":0.2,\"hours\":24},"
             "\"rain\":{\"enabled\":true,\"unavailable\":false,\"total_24h_inches\":0.2,"
             "\"total_72h_inches\":0.2,\"have_totals\":true},\"stations\":[],\"soil\":{}}");
    TEQ_I(zk_parse_kiosk(json, &k), ZK_OK);
    TEQ_I(zk_rain_strip_visible(&k), 0);
}

static void test_show_bars_false(void)
{
    zk_kiosk_t k;
    const char *json =
        "{\"now\":\"2026-09-29T06:52:00-06:00\",\"timezone\":\"America/Denver\",\"phase\":\"Idle\","
        "\"stations\":[{\"id\":\"az01\",\"title\":\"T\",\"color\":\"red\",\"on\":false,\"state\":\"idle\","
        "\"rain_pause_exempt\":false,\"soil_percent\":80}],"
        "\"soil\":{\"enabled\":true,\"et_known\":true,\"et_stale\":false,\"show_bars\":false}}";
    TEQ_I(zk_parse_kiosk(json, &k), ZK_OK);
    TEQ_I(k.stations[0].soil_percent, 80);
    TEQ_I(k.soil.show_bars, 0);
    TEQ_I(zk_station_soil_percent(&k, 0), -1);
}

static void test_garbage(void)
{
    zk_kiosk_t k;
    zk_schedules_t sch;
    TEQ_I(zk_parse_kiosk(NULL, &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("null", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("[]", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{]", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"phase\":", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk(NULL, NULL), ZK_ERR_ARG);
    TEQ_I(zk_parse_schedules("", &sch), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"pause\":1}", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"now\":123}", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"lockout\":\"yes\"}", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"stations\":{}}", &k), ZK_ERR_PARSE);
    TEQ_I(zk_parse_kiosk("{\"run\":\"nope\"}", &k), ZK_ERR_PARSE);
}

static void test_defaults(void)
{
    zk_kiosk_t k;
    zk_schedules_t sch;
    TEQ_I(zk_parse_kiosk("{}", &k), ZK_OK);
    TEQ_S(k.phase, "");
    TEQ_I(k.watering, 0);
    TEQ_I(k.fault, 0);
    TEQ_I(k.pause.paused, 0);
    TEQ_I(k.pause.has_until, 0);
    TEQ_I(k.rain.have_totals, 0);
    TEQ_I(k.has_run, 0);
    TEQ_I(zk_parse_kiosk("{\"phase\":null,\"pause\":null,\"stations_on\":null,\"run\":null,\"next_run\":null}", &k), ZK_OK);
    TEQ_S(k.phase, "");
    TEQ_I(k.pause.has_until, 0);
    TEQ_I(k.n_on, 0);
    TEQ_I(k.has_run, 0);
    TEQ_I(zk_parse_schedules("{\"schedules\":[{\"id\":\"x\",\"enabled\":true,\"start\":\"25:99\",\"weekdays\":[\"nope\"]}]}", &sch), ZK_OK);
    TEQ_I(sch.n, 1);
    TEQ_I(sch.items[0].has_start, 0);
    TEQ_I(sch.items[0].weekdays, 0);
}

static void test_huge(void)
{
    zk_kiosk_t k;
    TEQ_I(zk_parse_kiosk("{\"rain\":{\"enabled\":true,\"unavailable\":false,\"have_totals\":true,"
                         "\"total_72h_inches\":1e999,\"total_24h_inches\":0.2}}",
                         &k),
          ZK_OK);
    TEQ_I(k.rain.enabled, 1);
    TEQ_I(k.rain.have_totals, 0);
    TEQ_I(zk_parse_kiosk("{\"rain_strip\":{\"show\":true,\"inches\":1e999,\"hours\":24}}", &k), ZK_OK);
    TEQ_D(k.rain_strip.inches, 0);
    TEQ_I(zk_parse_kiosk("{\"stations\":[{\"id\":\"az01\",\"soil_percent\":150}]}", &k), ZK_OK);
    TEQ_I(k.stations[0].soil_percent, -1);
    TEQ_I(zk_parse_kiosk("{\"run\":{\"kind\":\"schedule\",\"program\":\"x\",\"step_index\":0,\"step_count\":1,"
                         "\"step_remaining_sec\":1e999,\"steps\":[]}}",
                         &k),
          ZK_OK);
    TEQ_I(k.has_run, 1);
    TEQ_I(k.run.step_remaining_sec, 0);
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

static void test_console_cursor(void)
{
    char path[256];
    char *got;
    int wr;
    wr = snprintf(path, sizeof path, "build/zk-tty-%d", (int)getpid());
    TCHECK(wr > 0, "path");
    unlink(path);
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        TCHECK(fd >= 0, "creat tty file");
        if (fd >= 0) {
            close(fd);
        }
    }
    TCHECK(setenv("ZK_TTY", path, 1) == 0, "setenv");
    zk_console_cursor(1);
    got = zk_read_file(path);
    TCHECK(got != NULL, "wrote hide");
    if (got) {
        TCHECK(strcmp(got, "\033[?25l") == 0, "hide seq [%s]", got);
        free(got);
    }
    {
        int fd = open(path, O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            close(fd);
        }
    }
    zk_console_cursor(0);
    got = zk_read_file(path);
    TCHECK(got != NULL, "wrote show");
    if (got) {
        TCHECK(strcmp(got, "\033[?25h") == 0, "show seq [%s]", got);
        free(got);
    }
    unlink(path);
    TCHECK(setenv("ZK_TTY", "build/zk-tty-missing-nope", 1) == 0, "missing path");
    zk_console_cursor(1);
    unsetenv("ZK_TTY");
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
    test_full_and_nulls();
    test_show_vs_totals();
    test_show_bars_false();
    test_garbage();
    test_defaults();
    test_huge();
    test_wall_parse();
    test_wall_advance();
    test_console_cursor();
    return zk_test_report("test_json");
}
