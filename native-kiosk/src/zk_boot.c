#include "zk_boot.h"

int zk_boot_repaint_due(int64_t now_ms, int64_t first_frame_ms, zk_boot_repaint_t *state)
{
    uint64_t elapsed;
    uint64_t need;

    if (!state || state->fired >= 2u) {
        return 0;
    }
    elapsed = (uint64_t)now_ms - (uint64_t)first_frame_ms;
    need = state->fired == 0u ? (uint64_t)ZK_BOOT_REPAINT_MS_1
                              : (uint64_t)ZK_BOOT_REPAINT_MS_2;
    if (elapsed < need) {
        return 0;
    }
    state->fired++;
    return 1;
}
