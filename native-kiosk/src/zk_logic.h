#ifndef ZK_LOGIC_H
#define ZK_LOGIC_H

#include "zk_model.h"

typedef struct {
    int have;
    char day[24];
    char time[16];
    char name[ZK_NOTE_MAX];
    char summary[64];
    int sched_index;
} zk_next_run_t;

typedef struct {
    int have;
    int step_index;    /* 0-based */
    int step_count;
    int remaining_sec; /* -1 unknown */
    int total_step_sec;
    char current_title_id[ZK_ID_MAX];
    char next_station_id[ZK_ID_MAX];
} zk_run_info_t;

typedef enum {
    ZK_PAUSE_TOMORROW = 0,
    ZK_PAUSE_DAYS2,
    ZK_PAUSE_WEEK,
    ZK_PAUSE_INDEFINITE
} zk_pause_kind_t;

typedef struct {
    char primary[48];
    char secondary[48];
    char body[96];
} zk_pause_preview_t;

int zk_rain_strip_visible(const zk_status_t *st);
void zk_rain_strip_text(const zk_status_t *st, char *out, size_t cap);

int zk_soil_percent(const zk_soil_t *soil, const char *station_id);
int zk_soil_low(int pct);

int zk_weekday_matches(unsigned mask, int wday);
int zk_in_season(const zk_schedule_t *sch, int y, int m, int d);

int zk_next_run(const zk_schedules_t *schedules, const zk_stations_t *stations,
                zk_wall_t now, zk_next_run_t *out);

int zk_run_infer(const zk_status_t *status, const zk_schedules_t *schedules,
                 zk_wall_t now, zk_run_info_t *info);

zk_wall_t zk_morning_cutoff(const zk_schedules_t *schedules, int y, int m, int d);
int zk_pause_preview(const zk_schedules_t *schedules, zk_wall_t now,
                     zk_pause_kind_t kind, zk_pause_preview_t *out);

void zk_pause_title(const zk_status_t *st, char *out, size_t cap);
void zk_pause_until_text(const zk_status_t *st, zk_wall_t now, char *out, size_t cap);

void zk_fmt_time12(int hh, int mm, char *out, size_t cap);
void zk_fmt_mmss(int sec, char *out, size_t cap);
void zk_fmt_step(int step_index0, int step_count, char *out, size_t cap);
void zk_fmt_inches(double v, char *out, size_t cap);

#endif
