#ifndef ZK_MODEL_H
#define ZK_MODEL_H

#include <stddef.h>
#include <stdint.h>

enum {
    ZK_OK = 0,
    ZK_RO = 1,
    ZK_ERR_ARG = -1,
    ZK_ERR_PARSE = -2,
    ZK_ERR_IO = -3,
    ZK_ERR_TIMEOUT = -4,
    ZK_ERR_OVERFLOW = -5,
    ZK_ERR_HTTP = -6,
    ZK_ERR_ADDR = -7
};

#define ZK_MAX_STATIONS  8
#define ZK_MAX_SCHEDULES 16
#define ZK_MAX_STEPS     16
#define ZK_MAX_ON        8

#define ZK_ID_MAX      32
#define ZK_TITLE_MAX   128
#define ZK_NOTE_MAX    96
#define ZK_COLOR_MAX   16
#define ZK_PHASE_MAX   32
#define ZK_TZ_MAX      64
#define ZK_ERRSTR_MAX  160
#define ZK_LABEL_MAX   96
#define ZK_REASON_MAX  32
#define ZK_SRC_MAX     32
#define ZK_STATE_MAX   16
#define ZK_KIND_MAX    16

#define ZK_WD_SUN (1u << 0)
#define ZK_WD_MON (1u << 1)
#define ZK_WD_TUE (1u << 2)
#define ZK_WD_WED (1u << 3)
#define ZK_WD_THU (1u << 4)
#define ZK_WD_FRI (1u << 5)
#define ZK_WD_SAT (1u << 6)
#define ZK_WD_ALL 0x7Fu

typedef struct {
    int y, m, d;
    int hh, mm, ss;
    int wday; /* 0=Sun .. 6=Sat */
} zk_wall_t;

typedef struct {
    int enabled;
    int unavailable;
    int have_totals;
    double total_24h;
    double total_72h;
} zk_rain_t;

typedef struct {
    int show;
    double inches;
    int hours;
} zk_rain_strip_t;

typedef struct {
    int paused;
    zk_wall_t until;
    int has_until;
    char label[ZK_LABEL_MAX];
    char reason[ZK_REASON_MAX];
    char source[ZK_SRC_MAX];
    double rain_inches;
} zk_pause_t;

typedef struct {
    char schedule_id[ZK_ID_MAX];
    char name[ZK_NOTE_MAX];
    zk_wall_t at;
    int has_at;
    zk_wall_t ends_at;
    int has_ends_at;
    int total_min;
    int skipped_by_pause;
} zk_kiosk_fire_t;

typedef struct {
    char station_id[ZK_ID_MAX];
    char title[ZK_TITLE_MAX];
    int planned_sec;
    int elapsed_sec;
    int remaining_sec;
    char state[ZK_STATE_MAX];
} zk_kiosk_run_step_t;

typedef struct {
    char kind[ZK_KIND_MAX];
    char program[ZK_NOTE_MAX];
    int step_index;
    int step_count;
    char current_station[ZK_ID_MAX];
    char next_station[ZK_ID_MAX];
    int step_elapsed_sec;
    int step_remaining_sec;
    int run_remaining_sec;
    int run_total_sec;
    zk_kiosk_run_step_t steps[ZK_MAX_STEPS];
    int n_steps;
} zk_kiosk_run_t;

typedef struct {
    char id[ZK_ID_MAX];
    char title[ZK_TITLE_MAX];
    char color[ZK_COLOR_MAX];
    int on;
    char state[ZK_STATE_MAX];
    int rain_pause_exempt;
    int soil_percent; /* -1 if null / unusable */
} zk_kiosk_station_t;

typedef struct {
    int enabled;
    int et_known;
    int et_stale;
    int show_bars;
    zk_wall_t updated_at;
    int has_updated_at;
} zk_kiosk_soil_t;

typedef struct {
    zk_wall_t now;
    int has_now;
    char timezone[ZK_TZ_MAX];
    char phase[ZK_PHASE_MAX];
    int watering;
    int fault;
    int lockout;
    char last_error[ZK_ERRSTR_MAX];
    char current_station[ZK_ID_MAX];
    char stations_on[ZK_MAX_ON][ZK_ID_MAX];
    int n_on;
    zk_pause_t pause;
    zk_rain_strip_t rain_strip;
    zk_rain_t rain;
    zk_kiosk_fire_t next_run;
    int has_next_run;
    zk_kiosk_fire_t next_effective;
    int has_next_effective;
    zk_kiosk_run_t run;
    int has_run;
    zk_kiosk_station_t stations[ZK_MAX_STATIONS];
    int n_stations;
    zk_kiosk_soil_t soil;
} zk_kiosk_t;

typedef struct {
    char station_id[ZK_ID_MAX];
    int minutes;
} zk_step_t;

typedef struct {
    char id[ZK_ID_MAX];
    char note[ZK_NOTE_MAX];
    int enabled;
    unsigned weekdays; /* bitmask; 0 = every day */
    int start_hh, start_mm;
    int has_start;
    zk_step_t steps[ZK_MAX_STEPS];
    int n_steps;
    int has_starts_on;
    int starts_y, starts_m, starts_d;
    int has_ends_on;
    int ends_y, ends_m, ends_d;
} zk_schedule_t;

typedef struct {
    zk_schedule_t items[ZK_MAX_SCHEDULES];
    int n;
} zk_schedules_t;

void zk_str_copy(char *dst, size_t cap, const char *src);
void zk_str_upper(char *s);
void zk_str_trim_copy(char *dst, size_t cap, const char *src);

int zk_parse_ymd(const char *s, int *y, int *m, int *d);
int zk_parse_wall(const char *rfc3339, zk_wall_t *out);
void zk_wall_set_wday(zk_wall_t *w);
zk_wall_t zk_wall_advance(zk_wall_t status_now, int64_t mono_now_ms, int64_t mono_at_poll_ms);
void zk_wall_add_seconds(zk_wall_t *w, int64_t sec);
int zk_days_from_civil(int y, int m, int d);
void zk_civil_from_days(int z, int *y, int *m, int *d);
int64_t zk_wall_epoch_sec(zk_wall_t w);
int zk_date_cmp(int y1, int m1, int d1, int y2, int m2, int d2);

int zk_parse_kiosk(const char *json, zk_kiosk_t *out);
int zk_parse_schedules(const char *json, zk_schedules_t *out);

#endif
