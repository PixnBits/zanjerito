#ifndef ZK_LAYOUT_H
#define ZK_LAYOUT_H

/* Direction C. 800x480. 9 mm at 267 ppi is 95 px. */
#define ZK_SCREEN_W    800
#define ZK_SCREEN_H    480
#define ZK_PX_PER_INCH 267
#define ZK_MIN_TAP     95
#define ZK_GAP_MIN     12
#define ZK_MAX_SHOWN   6

#define ZK_HERO_X 16
#define ZK_HERO_Y 16
#define ZK_HERO_W 376
#define ZK_HERO_H 332
#define ZK_GRID_X 404
#define ZK_GRID_Y 16
#define ZK_GRID_W 380
#define ZK_GRID_H 332
#define ZK_TILE_W 184
#define ZK_TILE_H4 160
#define ZK_TILE_H6 102
#define ZK_TILE_X0 404
#define ZK_TILE_X1 600
#define ZK_BAR_Y 360
#define ZK_PAUSE_X 16
#define ZK_PAUSE_W 376
#define ZK_PAUSE_H 104
#define ZK_STOP_X 404
#define ZK_STOP_W 380
#define ZK_STOP_H 104
#define ZK_SHEET_X 16
#define ZK_SHEET_Y 16
#define ZK_SHEET_W 768
#define ZK_SHEET_H 332
#define ZK_FULL_H 448
#define ZK_RAIN_X 40
#define ZK_RAIN_Y 264
#define ZK_RAIN_W 328
#define ZK_RAIN_H 64
/* 2-line hero label box (padding 22/18). Retry tick repaints this. */
#define ZK_LABEL_X 38
#define ZK_LABEL_Y 34
#define ZK_LABEL_W 332
#define ZK_LABEL_H 44
/* Soil slot: tile inset 14, 156x24. 6-up first row is y 69, stride 114. */
#define ZK_SLOT_INSET 14
#define ZK_SLOT_W 156
#define ZK_SLOT_H 24
#define ZK_SLOT_Y6 69
#define ZK_SLOT_STRIDE6 114
#define ZK_SLOT_DY4 85

enum zk_screen {
    ZK_SCREEN_HOME = 0,
    ZK_SCREEN_SHEET_RUN,
    ZK_SCREEN_SHEET_ANYWAY,
    ZK_SCREEN_SHEET_STOP_STATION,
    ZK_SCREEN_SHEET_STOP_ALL,
    ZK_SCREEN_SHEET_OVERLAP
};

enum zk_sheet {
    ZK_SHEET_NONE = 0,
    ZK_SHEET_RUN,
    ZK_SHEET_ANYWAY,
    ZK_SHEET_STOP_STATION,
    ZK_SHEET_STOP_ALL,
    ZK_SHEET_OVERLAP
};

enum zk_target_id {
    ZK_TARGET_NONE = 0,
    ZK_TARGET_STOP,
    ZK_TARGET_PAUSE,
    ZK_TARGET_RESUME,
    ZK_TARGET_HERO,
    ZK_TARGET_TILE0,
    ZK_TARGET_TILE1,
    ZK_TARGET_TILE2,
    ZK_TARGET_TILE3,
    ZK_TARGET_TILE4,
    ZK_TARGET_TILE5,
    ZK_TARGET_MINUS,
    ZK_TARGET_PLUS,
    ZK_TARGET_CANCEL,
    ZK_TARGET_RUN,
    ZK_TARGET_STOP_STATION,
    ZK_TARGET_STOP_ALL,
    ZK_TARGET_OK
};

enum zk_target_kind {
    ZK_KIND_NONE = 0,
    ZK_KIND_BUTTON,
    ZK_KIND_HERO,
    ZK_KIND_TILE,
    ZK_KIND_SHEET
};

typedef struct {
    int x, y, w, h;
} zk_rect;

typedef struct {
    enum zk_target_id id;
    zk_rect rect;
    enum zk_target_kind kind;
} zk_target;

typedef struct {
    int n_stations;
    int paused;
    int offline; /* tiles and Pause are not targets */
    int overlap; /* hero opens the read-only sheet */
    int minutes; /* stepper; minus is live only above 1 */
} zk_layout_in;

typedef struct {
    zk_rect screen;
    zk_rect hero;
    zk_rect pause;
    zk_rect stop;
    zk_rect rain;
    zk_rect label;
    zk_rect tiles[ZK_MAX_SHOWN];
    zk_rect slots[ZK_MAX_SHOWN];
    int n_tiles;
    int tile_h;
    zk_rect sheet;
    zk_rect minus;
    zk_rect value;
    zk_rect plus;
    zk_rect cancel;
    zk_rect run;
    zk_rect stop_station;
    zk_rect stop_all;
    zk_rect ok;
    zk_rect sheet_title;
} zk_layout_rects;

void zk_layout_rects_of(enum zk_screen screen, const zk_layout_in *in, zk_layout_rects *out);
int zk_layout_targets(enum zk_screen screen, const zk_layout_in *in, zk_target out[], int max);
enum zk_target_id zk_hit_test(const zk_target *targets, int n, int x, int y);
/* 1 when (x, y) is inside a live STOP target. 0 while the full sheet covers it. */
int zk_layout_hit_is_stop(enum zk_screen screen, const zk_layout_in *in, int x, int y);
int zk_layout_shown(int n_stations);
void zk_layout_slot(int n_stations, int index, zk_rect *out);

#endif
