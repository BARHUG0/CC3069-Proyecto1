#ifndef SPAWN_H
#define SPAWN_H

#include "ecs.h"
#include "rng.h"

typedef struct StarField {
    float accumulator;
    float spawnRate;
    int liveStars;
    int targetStars;
    float screenW;
    float screenH;
} StarField;

void starfield_init(StarField *sf, int targetStars, float screenW, float screenH);
Entity spawn_star(World *w, Rng *rng, float screenW, float screenH);

#endif /* SPAWN_H */
