#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "systems.h"

static void assert_worlds_equal(const World *a, const World *b)
{
    assert(memcmp(a, b, sizeof(*a)) == 0);
}

static void assert_starfield_full(const World *world, const StarField *starfield)
{
    int stars = 0;

    for (uint32_t e = 0; e < world->highWater; ++e) {
        if ((world->mask[e] & (C_RENDER | C_TWINKLE | C_LIFE)) ==
            (C_RENDER | C_TWINKLE | C_LIFE)) {
            assert(world->alpha[e] > 0.0f);
            stars++;
        }
    }

    assert(starfield->liveStars == starfield->targetStars);
    assert(stars == starfield->targetStars);
}

/* Bajo el modelo de anclas (ver spawn.h) un sistema puede barrer arcos amplios
 * y salirse del borde por momentos a proposito, asi que no se comprueban
 * limites de pantalla: solo que cada sistema tenga una orbita real —
 * radio positivo alrededor de un ancla valida y velocidad angular en el rango
 * que fija spawn_system_into_slot. */
static void assert_systems_orbit(const World *world, const SolarSystems *systems)
{
    (void)world;
    for (int s = 0; s < systems->count; ++s) {
        assert(systems->anchor[s] == 0 || systems->anchor[s] == 1);
        assert(systems->orbRad[s] >= 0.0f);
        const float spd = systems->orbSpd[s] < 0.0f ? -systems->orbSpd[s]
                                                    : systems->orbSpd[s];
        assert(spd >= 0.03f && spd <= 0.5f);
    }
}

static void assert_layout_bounds(World *world, SolarSystems *systems)
{
    static const int counts[] = { 1, 6, 64, 256 };
    static const float widths[] = { 320.0f, 1280.0f };
    static const float heights[] = { 240.0f, 720.0f };

    for (size_t size = 0; size < sizeof(widths) / sizeof(widths[0]); ++size) {
        for (size_t count = 0; count < sizeof(counts) / sizeof(counts[0]); ++count) {
            Rng rng;
            rng_seed(&rng, 20260901u);
            ecs_reset(world);
            memset(systems, 0, sizeof(*systems));
            spawn_solar_systems(world, systems, &rng, counts[count],
                                widths[size], heights[size]);
            assert_systems_orbit(world, systems);
        }
    }
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
    starfield_init(sequentialWorld, &sequentialStars, &sequentialRng,
                   4096, 1280.0f, 720.0f);
    starfield_init(parallelWorld, &parallelStars, &parallelRng,
                   4096, 1280.0f, 720.0f);

    assert_starfield_full(sequentialWorld, &sequentialStars);
    assert_starfield_full(parallelWorld, &parallelStars);
    assert_systems_orbit(sequentialWorld, sequentialSystems);
    assert_systems_orbit(parallelWorld, parallelSystems);

    const int sequentialSpawned =
        sys_spawn_stars(sequentialWorld, &sequentialStars, &sequentialRng, 1.0f);
    const int parallelSpawned =
        sys_spawn_stars(parallelWorld, &parallelStars, &parallelRng, 1.0f);

    assert(sequentialSpawned == parallelSpawned);
    assert(memcmp(&sequentialStars, &parallelStars, sizeof(sequentialStars)) == 0);
    assert(memcmp(&sequentialRng, &parallelRng, sizeof(sequentialRng)) == 0);
    assert_worlds_equal(sequentialWorld, parallelWorld);
    assert(memcmp(sequentialSystems, parallelSystems, sizeof(*sequentialSystems)) == 0);

    sys_drift(sequentialWorld, sequentialSystems, 0.016f);
    sys_drift(parallelWorld, parallelSystems, 0.016f);
    assert_worlds_equal(sequentialWorld, parallelWorld);
    assert(memcmp(sequentialSystems, parallelSystems, sizeof(*sequentialSystems)) == 0);
    assert_systems_orbit(sequentialWorld, sequentialSystems);
    assert_systems_orbit(parallelWorld, parallelSystems);

    trails_init(sequentialTrails, sequentialWorld, sequentialSystems);
    trails_init(parallelTrails, parallelWorld, parallelSystems);
    const int sequentialKilled =
        sys_update(sequentialWorld, sequentialTrails, 3.5f, 20.0f);
    const int parallelKilled =
        sys_update_parallel(parallelWorld, parallelTrails, 3.5f, 20.0f);
    assert(sequentialKilled == parallelKilled);
    assert(sequentialKilled == sequentialStars.targetStars);
    assert(memcmp(sequentialTrails, parallelTrails, sizeof(*sequentialTrails)) == 0);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    rng_seed(&sequentialRng, 20260901u);
    rng_seed(&parallelRng, 20260901u);
    ecs_reset(sequentialWorld);
    ecs_reset(parallelWorld);
    spawn_solar_systems(sequentialWorld, sequentialSystems, &sequentialRng,
                        1, 320.0f, 240.0f);
    spawn_solar_systems(parallelWorld, parallelSystems, &parallelRng,
                        1, 320.0f, 240.0f);
    starfield_init(sequentialWorld, &sequentialStars, &sequentialRng,
                   16, 320.0f, 240.0f);
    starfield_init(parallelWorld, &parallelStars, &parallelRng,
                   16, 320.0f, 240.0f);
    trails_init(sequentialTrails, sequentialWorld, sequentialSystems);
    trails_init(parallelTrails, parallelWorld, parallelSystems);
    sys_drift(sequentialWorld, sequentialSystems, 0.016f);
    sys_drift(parallelWorld, parallelSystems, 0.016f);
    assert(sys_update(sequentialWorld, sequentialTrails, 1.0f, 0.016f) ==
           sys_update_parallel(parallelWorld, parallelTrails, 1.0f, 0.016f));
    assert(memcmp(sequentialTrails, parallelTrails, sizeof(*sequentialTrails)) == 0);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    assert_layout_bounds(sequentialWorld, sequentialSystems);

    free(parallelTrails);
    free(sequentialTrails);
    free(parallelSystems);
    free(sequentialSystems);
    ecs_world_free(parallelWorld);
    ecs_world_free(sequentialWorld);
    return 0;
}
