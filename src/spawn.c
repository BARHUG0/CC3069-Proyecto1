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
    w->alpha[e] = 0.0f; /* el sistema de vida la hace aparecer con fundido */

    /* Centelleo: frecuencia y fase aleatorias para que no parpadeen en bloque. */
    w->twFreq[e]  = rng_range(rng, 0.6f, 4.5f);
    w->twPhase[e] = rng_range(rng, 0.0f, 6.2831853f);
    w->twBase[e]  = rng_range(rng, 0.45f, 0.75f);
    w->twAmp[e]   = rng_range(rng, 0.20f, 0.50f);

    w->lifeMax[e] = rng_range(rng, 4.0f, 14.0f);
    w->life[e]    = w->lifeMax[e];

    w->mask[e] = C_POS | C_RENDER | C_TWINKLE | C_LIFE;
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

int sys_spawn_stars(World *w, StarField *sf, Rng *rng, float dt)
{
    sf->accumulator += dt * sf->spawnRate;

    int budget = (int)sf->accumulator;
    if (budget <= 0) {
        return 0;
    }
    sf->accumulator -= (float)budget;

    int maxPerFrame = sf->targetStars / 16;
    if (maxPerFrame < 64) {
        maxPerFrame = 64;
    }
    if (budget > maxPerFrame) {
        budget = maxPerFrame;
    }

    int spawned = 0;
    for (int i = 0; i < budget; ++i) {
        if (sf->liveStars >= sf->targetStars) {
            break;
        }
        if (spawn_star(w, rng, sf->screenW, sf->screenH) == ECS_INVALID) {
            break;
        }
        sf->liveStars++;
        spawned++;
    }
    return spawned;
}
