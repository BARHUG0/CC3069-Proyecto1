#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "systems.h"

static void assert_worlds_equal(const World *a, const World *b)
{
    assert(memcmp(a, b, sizeof(*a)) == 0);
}

int main(void)
{
    World *sequentialWorld = ecs_world_alloc();
    World *parallelWorld = ecs_world_alloc();
    SolarSystems *sequentialSystems = calloc(1, sizeof(*sequentialSystems));
    SolarSystems *parallelSystems = calloc(1, sizeof(*parallelSystems));
    TrailBuffer *sequentialTrails = calloc(1, sizeof(*sequentialTrails));
    TrailBuffer *parallelTrails = calloc(1, sizeof(*parallelTrails));

    assert(sequentialWorld != NULL);
    assert(parallelWorld != NULL);
    assert(sequentialSystems != NULL);
    assert(parallelSystems != NULL);
    assert(sequentialTrails != NULL);
    assert(parallelTrails != NULL);

    Rng sequentialRng;
    Rng parallelRng;
    rng_seed(&sequentialRng, 20260901u);
    rng_seed(&parallelRng, 20260901u);

    spawn_solar_systems(sequentialWorld, sequentialSystems, &sequentialRng,
                        64, 1280.0f, 720.0f);
    spawn_solar_systems(parallelWorld, parallelSystems, &parallelRng,
                        64, 1280.0f, 720.0f);

    StarField sequentialStars;
    StarField parallelStars;
    starfield_init(&sequentialStars, 4096, 1280.0f, 720.0f);
    starfield_init(&parallelStars, 4096, 1280.0f, 720.0f);

    const int sequentialSpawned =
        sys_spawn_stars(sequentialWorld, &sequentialStars, &sequentialRng, 1.0f);
    const int parallelSpawned =
        sys_spawn_stars_parallel(parallelWorld, &parallelStars, &parallelRng, 1.0f);

    assert(sequentialSpawned == parallelSpawned);
    assert(memcmp(&sequentialStars, &parallelStars, sizeof(sequentialStars)) == 0);
    assert(memcmp(&sequentialRng, &parallelRng, sizeof(sequentialRng)) == 0);
    assert_worlds_equal(sequentialWorld, parallelWorld);
    assert(memcmp(sequentialSystems, parallelSystems, sizeof(*sequentialSystems)) == 0);

    sys_drift(sequentialWorld, sequentialSystems, 0.016f);
    sys_drift_parallel(parallelWorld, parallelSystems, 0.016f);
    assert_worlds_equal(sequentialWorld, parallelWorld);
    assert(memcmp(sequentialSystems, parallelSystems, sizeof(*sequentialSystems)) == 0);

    sys_twinkle(sequentialWorld, 3.5f);
    sys_twinkle_parallel(parallelWorld, 3.5f);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    sys_orbit(sequentialWorld, 0.016f);
    sys_orbit_parallel(parallelWorld, 0.016f);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    trails_init(sequentialTrails, sequentialWorld, sequentialSystems);
    trails_init(parallelTrails, parallelWorld, parallelSystems);
    sys_trails(sequentialWorld, sequentialTrails, 0.1f);
    sys_trails_parallel(parallelWorld, parallelTrails, 0.1f);
    assert(memcmp(sequentialTrails, parallelTrails, sizeof(*sequentialTrails)) == 0);

    const int sequentialKilled = sys_lifetime(sequentialWorld, 20.0f);
    const int parallelKilled = sys_lifetime_parallel(parallelWorld, 20.0f);
    assert(sequentialKilled == parallelKilled);
    assert(sequentialKilled == sequentialSpawned);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    free(parallelTrails);
    free(sequentialTrails);
    free(parallelSystems);
    free(sequentialSystems);
    ecs_world_free(parallelWorld);
    ecs_world_free(sequentialWorld);
    return 0;
}
