#ifndef ZK_LAYOUT_H
#define ZK_LAYOUT_H

#define ZK_SCREEN_W    800
#define ZK_SCREEN_H    480
#define ZK_PX_PER_INCH 267
#define ZK_MIN_TAP     107
#define ZK_STOP_MIN_H  240

enum zk_screen {
    ZK_SCREEN_HOME = 0,
    ZK_SCREEN_RUNNING,
    ZK_SCREEN_PAUSED,
    ZK_SCREEN_PICKER,
    ZK_SCREEN_SCHEDULES,
    ZK_SCREEN_STATION,
    ZK_SCREEN_NEEDS_UPDATE,
    ZK_SCREEN_CONFIRM_STOP,
    ZK_SCREEN_CONFIRM_PAUSE
};

enum zk_target_id {
    ZK_TARGET_NONE = 0,
    ZK_TARGET_STOP,
    ZK_TARGET_PAUSE,
    ZK_TARGET_RESUME,
    ZK_TARGET_BACK,
    ZK_TARGET_HEADER_NEXT,
    ZK_TARGET_TILE0,
    ZK_TARGET_TILE1,
    ZK_TARGET_TILE2,
    ZK_TARGET_TILE3,
    ZK_TARGET_TILE4,
    ZK_TARGET_TILE5,
    ZK_TARGET_TILE6,
    ZK_TARGET_TILE7,
    ZK_TARGET_CHIP0,
    ZK_TARGET_CHIP1,
    ZK_TARGET_CHIP2,
    ZK_TARGET_CHIP3,
    ZK_TARGET_INFO_CARD,
    ZK_TARGET_HOLD_EDIT,
    ZK_TARGET_MODAL_OK,
    ZK_TARGET_MODAL_CANCEL,
    ZK_TARGET_SCHEDULES
};

enum zk_target_kind {
    ZK_KIND_NONE = 0,
    ZK_KIND_BUTTON,
    ZK_KIND_HEADER,
    ZK_KIND_TILE,
    ZK_KIND_CHIP,
    ZK_KIND_CARD,
    ZK_KIND_MODAL
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
    int rain_visible;
    int n_schedules;
} zk_layout_in;

typedef struct {
    zk_rect screen;
    zk_rect main;
    zk_rect rail;
    zk_rect stop;
    zk_rect slot;
    zk_rect header;
    zk_rect rain;
    zk_rect banner;
    zk_rect info;
    zk_rect title;
    zk_rect card;
    zk_rect hold_edit;
    zk_rect tiles[8];
    int n_tiles;
    zk_rect chips[4];
    zk_rect modal;
    zk_rect modal_ok;
    zk_rect modal_cancel;
    zk_rect step_strip;
    zk_rect sched_header;
    zk_rect sched_rows[5];
    int n_sched_rows;
    zk_rect schedules;
} zk_layout_rects;

void zk_layout_rects_of(enum zk_screen screen, const zk_layout_in *in, zk_layout_rects *out);
int zk_layout_targets(enum zk_screen screen, const zk_layout_in *in, zk_target out[], int max);
enum zk_target_id zk_hit_test(const zk_target *targets, int n, int x, int y);
/* 1 when (x, y) is inside the STOP target on this screen. Same rule the kiosk uses. */
int zk_layout_hit_is_stop(enum zk_screen screen, const zk_layout_in *in, int x, int y);

#endif
