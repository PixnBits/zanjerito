#include "zk_test.h"
#include "zk_boot.h"

#include <stdint.h>
#include <string.h>

static void test_before_delay(void)
{
    zk_boot_repaint_t st;

    memset(&st, 0, sizeof st);
    TEQ_I(zk_boot_repaint_due(0, 0, &st), 0);
    TEQ_I(st.fired, 0);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_1 - 1, 0, &st), 0);
    TEQ_I(st.fired, 0);
    TEQ_I(zk_boot_repaint_due(4999, 3000, &st), 0); /* elapsed 1999 */
    TEQ_I(st.fired, 0);
}

static void test_first_shot_once(void)
{
    zk_boot_repaint_t st;

    memset(&st, 0, sizeof st);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_1, 0, &st), 1);
    TEQ_I(st.fired, 1);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_1, 0, &st), 0);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_2 - 1, 0, &st), 0);
    TEQ_I(st.fired, 1);
}

static void test_second_shot_once(void)
{
    zk_boot_repaint_t st;

    memset(&st, 0, sizeof st);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_1, 0, &st), 1);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_2, 0, &st), 1);
    TEQ_I(st.fired, 2);
    TEQ_I(zk_boot_repaint_due(ZK_BOOT_REPAINT_MS_2, 0, &st), 0);
    TEQ_I(zk_boot_repaint_due(999999, 0, &st), 0);
    TEQ_I(st.fired, 2);
}

static void test_jump_fires_one_per_call(void)
{
    zk_boot_repaint_t st;

    memset(&st, 0, sizeof st);
    TEQ_I(zk_boot_repaint_due(100000, 0, &st), 1);
    TEQ_I(st.fired, 1);
    TEQ_I(zk_boot_repaint_due(100000, 0, &st), 1);
    TEQ_I(st.fired, 2);
    TEQ_I(zk_boot_repaint_due(100000, 0, &st), 0);
}

static void test_null_state(void)
{
    TEQ_I(zk_boot_repaint_due(10000, 0, NULL), 0);
}

static void test_clock_wrap(void)
{
    zk_boot_repaint_t st;
    int64_t first;
    int64_t now;

    memset(&st, 0, sizeof st);
    /* first is ZK_BOOT_REPAINT_MS_1 before unsigned 0, so now=0 is exactly due. */
    first = (int64_t)(UINT64_C(0) - (uint64_t)ZK_BOOT_REPAINT_MS_1);
    now = 0;
    TEQ_I(zk_boot_repaint_due(now - 1, first, &st), 0);
    TEQ_I(st.fired, 0);
    TEQ_I(zk_boot_repaint_due(now, first, &st), 1);
    TEQ_I(st.fired, 1);
    TEQ_I(zk_boot_repaint_due(now, first, &st), 0);

    now = (int64_t)(ZK_BOOT_REPAINT_MS_2 - ZK_BOOT_REPAINT_MS_1);
    TEQ_I(zk_boot_repaint_due(now - 1, first, &st), 0);
    TEQ_I(st.fired, 1);
    TEQ_I(zk_boot_repaint_due(now, first, &st), 1);
    TEQ_I(st.fired, 2);
    TEQ_I(zk_boot_repaint_due(now + 100000, first, &st), 0);
    TEQ_I(st.fired, 2);
}

int main(void)
{
    test_before_delay();
    test_first_shot_once();
    test_second_shot_once();
    test_jump_fires_one_per_call();
    test_null_state();
    test_clock_wrap();
    return zk_test_report("test_boot");
}
