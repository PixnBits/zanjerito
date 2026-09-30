#ifndef ZK_LOGIC_H
#define ZK_LOGIC_H

#include "zk_model.h"

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

int zk_rain_strip_visible(const zk_kiosk_t *k);
void zk_rain_strip_text(const zk_rain_strip_t *rs, char *out, size_t cap);

int zk_station_soil_percent(const zk_kiosk_t *k, int station_index);
int zk_soil_low(int pct);

int zk_weekday_matches(unsigned mask, int wday);
int zk_in_season(const zk_schedule_t *sch, int y, int m, int d);

zk_wall_t zk_morning_cutoff(const zk_schedules_t *schedules, int y, int m, int d);
int zk_pause_preview(const zk_schedules_t *schedules, zk_wall_t now,
                     zk_pause_kind_t kind, zk_pause_preview_t *out);

void zk_pause_title(const zk_pause_t *p, char *out, size_t cap);
void zk_pause_until_text(const zk_pause_t *p, zk_wall_t now, char *out, size_t cap);

void zk_fmt_time12(int hh, int mm, char *out, size_t cap);
void zk_fmt_mmss(int sec, char *out, size_t cap);
void zk_fmt_step(int step_index0, int step_count, char *out, size_t cap);
void zk_fmt_inches(double v, char *out, size_t cap);
void zk_fmt_relative_day(zk_wall_t at, zk_wall_t now, char *out, size_t cap);
void zk_fmt_fire_when(const zk_kiosk_fire_t *fire, zk_wall_t now, char *out, size_t cap);
void zk_fmt_name_upper(const char *name, char *out, size_t cap);

const zk_kiosk_station_t *zk_kiosk_station(const zk_kiosk_t *k, const char *id);
int zk_kiosk_station_index(const zk_kiosk_t *k, const char *id);

#endif
