#ifndef ZK_UI_H
#define ZK_UI_H

#include "app.h"
#include "lvgl.h"

void zk_ui_init(lv_display_t *disp, const zk_app_t *app);
void zk_ui_tick(void);

/* screen: zk_screen, or -1 to follow status. pressed_target: zk_target_id or 0.
 * pick_chip: -1, or 0..3 for the pause confirm copy. */
void zk_ui_debug_set_view(int screen, int pressed_target, int pick_chip);
/* Rebuild from the current snapshot and flush immediately. */
void zk_ui_refresh(void);
/* Remember a fixture directory for later zk_data_start calls. Copies the path. */
void zk_ui_set_fixture(const char *dir);

#endif
