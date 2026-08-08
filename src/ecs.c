#include "ecs.h"
#include <stdlib.h>
#include <string.h>

World *ecs_world_alloc(void)
{
    World *w = (World *)calloc(1, sizeof(World));
    if (w) {
        w->capacity = ECS_MAX_ENTITIES;
        w->highWater = 0;
        w->alive = 0;
        w->freeCount = 0;
    }
    return w;
}

void ecs_world_free(World *w)
{
    free(w);
}

void ecs_reset(World *w)
{
    memset(w->mask, 0, w->highWater * sizeof(uint32_t));
    w->highWater = 0;
    w->alive = 0;
    w->freeCount = 0;
}
