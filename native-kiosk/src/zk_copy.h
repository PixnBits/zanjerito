#ifndef ZK_COPY_H
#define ZK_COPY_H

#include "zk_extra.h"
#include "zk_flow.h"
#include "zk_layout.h"
#include "zk_soilcfg.h"

#include <stddef.h>

#define ZK_NBSP "\xC2\xA0"
#define ZK_MID "\xC2\xB7"
#define ZK_EN "\xE2\x80\x93"
#define ZK_RQ "\xE2\x80\xBA"
#define ZK_RAIN_TRACK 328
#define ZK_PAUSE_BODY "{\"until\":\"tomorrow_morning\",\"reason\":\"rain\"}"
#define ZK_WORD_PAUSE "Pause for rain"
#define ZK_WORD_RESUME "Resume"
#define ZK_WORD_SCHEDULES "Schedules"
#define ZK_WORD_HOME "Home"
#define ZK_WORD_NEXT "Next"
#define ZK_SCHED_NOTE "Fix on phone: Zanjerito " ZK_RQ " Schedules."
#define ZK_SCHED_EMPTY "Nothing scheduled"
#define ZK_SCHED_EMPTY_BODY "Add a program on your phone"
#define ZK_SCHED_READ "SCHEDULES " ZK_MID " READ ONLY"
/* Explicit break. "confirmed yet" is 327 px in the 332 px column. */
#define ZK_STOP_PENDING_HEADLINE "Stop not\nconfirmed" ZK_NBSP "yet"
#define ZK_STOP_PENDING_LABEL "STOP " ZK_MID " WAITING FOR CONTROLLER"
#define ZK_STOP_PENDING_RETRY "Retrying until all valves are off."
#define ZK_STOP_PENDING_OFFLINE "Can't reach controller. Retrying\xE2\x80\xA6"
#define ZK_WORD_STOP "STOP"
#define ZK_WORD_CANCEL "Cancel"
#define ZK_WORD_RUN "Run"
#define ZK_WORD_STOP_ALL "Stop all"
#define ZK_WORD_OK "OK"

typedef enum {
    ZK_HERO_IDLE = 0,
    ZK_HERO_WATERING,
    ZK_HERO_PAUSED,
    ZK_HERO_NOTHING,
    ZK_HERO_OVERLAP,
    ZK_HERO_OFFLINE,
    ZK_HERO_STOP_PENDING,
    ZK_HERO_STOP_CONFIRMED,
    ZK_HERO_MANUAL
} zk_hero_style_t;

typedef enum {
    ZK_TILE_OFF = 0,
    ZK_TILE_ON,
    ZK_TILE_UNAVAILABLE
} zk_tile_vis_t;

typedef enum {
    ZK_SOIL_HIDDEN = 0,
    ZK_SOIL_NONE,
    ZK_SOIL_VALUE,
    ZK_SOIL_STALE
} zk_soil_vis_t;

typedef struct {
    char label[160];
    char headline[192];
    char time_line[96];
    char secondary[240];
    zk_hero_style_t style;
    int rain;
    char rain_text[72];
    int rain_fill_px;
} zk_hero_copy_t;

typedef struct {
    char name[ZK_TITLE_MAX];
    char slot[96];
    zk_tile_vis_t vis;
    zk_soil_vis_t soil;
    int percent; /* 0..100 bar fill, or -1 when the bar is empty */
    int muted_slot;
} zk_tile_copy_t;

typedef struct {
    char label[120];
    char title[220];
    char body[480];
    char time_line[96];
    int minutes;
    int show_stepper;
    int minus_enabled;
} zk_sheet_copy_t;

typedef struct {
    char name[ZK_NOTE_MAX];
    char meta[192];
    int enabled;
    int on;
} zk_sched_row_t;

typedef struct {
    char label[160];
    char note[120];
    char empty_title[48];
    char empty_body[64];
    zk_sched_row_t rows[2];
    int n_rows;
    int n_total;
    int next_on;
} zk_sched_copy_t;

typedef struct {
    zk_hero_copy_t hero;
    zk_tile_copy_t tiles[ZK_MAX_SHOWN];
    int n_tiles;
    zk_sheet_copy_t sheet;
    int sheet_kind;
    char bar_word[24];
    int bar_is_resume;
    int bar_disabled;
    int stop_filled;
    int overlap;
    char overlap_title[96];
    char overlap_body[480];
    zk_sched_copy_t sched;
} zk_view_t;

typedef int (*zk_width_fn)(const char *text, size_t nbytes, void *user);

typedef struct {
    const zk_kiosk_t *kiosk;
    int have_kiosk;
    const zk_schedules_t *schedules;
    int have_schedules;
    int unreachable; /* no fresh snapshot, poll failed, or 404 */
    zk_wall_t wall;
    const zk_soil_doc_t *soil_doc;
    const zk_last_run_t *lasts;
    int n_lasts;
} zk_copy_src_t;

typedef struct {
    int sheet_kind;
    int station_index;
    int minutes;
    zk_stop_phase_t stop_phase;
    int offline_pending; /* pending copy, not "Watering stopped" */
    zk_wall_t confirmed_at;
    int has_confirmed_at;
    const zk_soil_cfg_t *soil_cfg;
    int label_px; /* 0 skips clamping */
    int tile_px;
    zk_width_fn width_px;
    void *width_user;
    int sched_index; /* first program shown; the screen shows two from here */
} zk_view_in_t;

void zk_fmt_time_nbsp(int hh, int mm, char *out, size_t cap);
void zk_fmt_inches_trim(double v, char *out, size_t cap);
int zk_usual_minutes(const zk_copy_src_t *src, int station_index);
void zk_view_build(const zk_copy_src_t *src, const zk_view_in_t *in, zk_view_t *out);

#endif
