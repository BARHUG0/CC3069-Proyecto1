#include "spawn.h"

static const uint8_t STAR_PALETTE[][3] = {
    { 255, 255, 255 },
    { 255, 255, 255 },
    { 202, 222, 255 },
    { 170, 200, 255 },
    { 255, 226, 180 },
    { 255, 196, 170 }
};
#define STAR_PALETTE_N (sizeof(STAR_PALETTE) / sizeof(STAR_PALETTE[0]))

Entity spawn_star(World *w, Rng *rng, float screenW, float screenH)
{
    Entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    const uint32_t p = rng_below(rng, (uint32_t)STAR_PALETTE_N);

    w->px[e] = rng_range(rng, 0.0f, screenW);
    w->py[e] = rng_range(rng, 0.0f, screenH);
    w->rad[e]   = rng_range(rng, 0.6f, 1.9f);
    w->cr[e]    = STAR_PALETTE[p][0];
    w->cg[e]    = STAR_PALETTE[p][1];
    w->cb[e]    = STAR_PALETTE[p][2];
    w->alpha[e] = 1.0f;

    w->mask[e] = C_POS | C_RENDER;
    return e;
}

void starfield_init(StarField *sf, int targetStars, float screenW, float screenH)
{
    sf->accumulator = 0.0f;
    sf->targetStars = targetStars;
    sf->liveStars   = 0;
    sf->screenW     = screenW;
    sf->screenH     = screenH;
    sf->spawnRate = (float)targetStars / 9.0f;
}
