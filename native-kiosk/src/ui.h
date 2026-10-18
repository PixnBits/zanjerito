#ifndef ZK_UI_H
#define ZK_UI_H

#include "app.h"
#include "lvgl.h"
#include "zk_power.h"

void zk_ui_init(lv_display_t *disp, const zk_app_t *app);
void zk_ui_tick(void);
/* 1 when the current screen's STOP target contains this point. 0 until the first tick. */
int zk_ui_hit_is_stop(int x, int y);
/* Snapshot plus the open confirm modal. NULL out is ignored. */
void zk_ui_power_inputs(zk_power_inputs_t *out);

/* screen: zk_screen, or -1 to follow status. pressed_target: zk_target_id or 0.
 * pick_chip: -1, or 0..3 for the pause confirm copy. */
void zk_ui_debug_set_view(int screen, int pressed_target, int pick_chip);
/* Rebuild from the current snapshot and flush immediately. */
void zk_ui_refresh(void);
/* Remember a fixture directory for later zk_data_start calls. Copies the path. */
void zk_ui_set_fixture(const char *dir);

#define ZK_UI_LABEL_ID_MAX 32
#define ZK_UI_LABEL_TEXT_MAX 192

/* One label after lv_obj_update_layout. x/y/w/h and parent_* are absolute
 * pixels (w/h are exclusive extents). y_rel is the label's y inside its parent. */
typedef struct {
    char id[ZK_UI_LABEL_ID_MAX];
    int x, y, w, h;
    int y_rel;
    int parent_x, parent_y, parent_w, parent_h;
    int visible;
    int clamped;
    char text[ZK_UI_LABEL_TEXT_MAX];
} zk_ui_label_box_t;

/* Writes one entry per label built for the current screen. Returns the count,
 * or -1 if the tracker overflowed or cap is too small. */
int zk_ui_debug_label_boxes(zk_ui_label_box_t *out, int cap);

/* Home station tile after layout. bg and border are 0xRRGGBB. title_w is the
 * name widget width (same slot as main: tile width minus 28). */
typedef struct {
    int present;
    int on;
    uint32_t bg;
    int border_w;
    uint32_t border;
    int drop_visible;
    int title_w;
    int tile_w;
} zk_ui_tile_dbg_t;

/* Fills out for home tile index. Returns 1 when that tile exists. */
int zk_ui_debug_tile(int index, zk_ui_tile_dbg_t *out);

#endif
