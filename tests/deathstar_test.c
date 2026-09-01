#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "deathstar.h"

int main(void)
{
    DeathStar ds;
    memset(&ds, 0, sizeof(ds));
    ds.cam.position.z = 6.0f;
    ds.cam.fovy = 45.0f;
    ds.worldR = 0.5f;
    ds.frontEaseDeg = 65.0f;
    ds.phase = DS_IDLE;
    ds.victim = -1;

    deathstar_center(&ds, 800.0f, 600.0f);
    assert(ds.posX == 400.0f);
    assert(ds.posY == 300.0f);

    deathstar_move(&ds, 1.0f, 1.0f, 800.0f, 600.0f, 1.0f);
    const float diagonalStep = DS_MOVE_PX_S / sqrtf(2.0f);
    assert(fabsf(ds.posX - 400.0f - diagonalStep) < 0.01f);
    assert(fabsf(ds.posY - 300.0f - diagonalStep) < 0.01f);

    deathstar_move(&ds, -1.0f, -1.0f, 800.0f, 600.0f, 100.0f);
    assert(ds.posX > 0.0f);
    assert(ds.posY > 0.0f);

    SolarSystems ss;
    memset(&ss, 0, sizeof(ss));
    ss.count = 3;
    ss.cellR = 40.0f;
    for (int i = 0; i < ss.count; ++i) {
        ss.cx[i] = 100.0f + 100.0f * (float)i;
        ss.cy[i] = 200.0f + 50.0f * (float)i;
        ss.layer[i] = i % SYS_LAYER_COUNT;
    }

    Rng rng;
    rng_seed(&rng, 123u);
    assert(deathstar_fire(&ds, &ss, &rng) == 1);
    assert(ds.victim >= 0 && ds.victim < ss.count);
    assert(ds.aimX == ss.cx[ds.victim]);
    assert(ds.aimY == ss.cy[ds.victim]);
    assert(deathstar_fire(&ds, &ss, &rng) == 0);

    World *world = ecs_world_alloc();
    TrailBuffer *tb = calloc(1, sizeof(*tb));
    assert(world != NULL);
    assert(tb != NULL);

    deathstar_update(&ds, world, &ss, tb, &rng, 2, 800.0f, 600.0f, 1.0f);
    assert(ds.phase == DS_FIRE);
    assert(ds.victim == -1);
    assert(ds.kills == 1);
    assert(ss.count == 2);

    free(tb);
    ecs_world_free(world);
    return 0;
}
