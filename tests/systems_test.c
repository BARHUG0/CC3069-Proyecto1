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

static void assert_systems_visible(const World *world, const SolarSystems *systems,
                                   float screenW, float screenH)
{
    for (int s = 0; s < systems->count; ++s) {
        float reach = 0.0f;
        const int first = systems->ringFirst[s];
        const int last = first + systems->planetCount[s];

        for (int i = first; i < last; ++i) {
            const Entity e = systems->ringEntity[i];
            const float x = systems->ringRx[i] + world->rad[e];
            const float y = systems->ringRy[i] + world->rad[e];
            if (x > reach) reach = x;
            if (y > reach) reach = y;
        }

        assert(systems->cx[s] - reach >= 0.0f);
        assert(systems->cx[s] + reach <= screenW);
        assert(systems->cy[s] - reach >= 0.0f);
        assert(systems->cy[s] + reach <= screenH);
        assert(systems->homeX[s] - systems->orbRad[s] - reach >= 0.0f);
        assert(systems->homeX[s] + systems->orbRad[s] + reach <= screenW);
        assert(systems->homeY[s] - systems->orbRad[s] - reach >= 0.0f);
        assert(systems->homeY[s] + systems->orbRad[s] + reach <= screenH);
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
            assert_systems_visible(world, systems, widths[size], heights[size]);
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
    assert_systems_visible(sequentialWorld, sequentialSystems, 1280.0f, 720.0f);
    assert_systems_visible(parallelWorld, parallelSystems, 1280.0f, 720.0f);

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
    assert_systems_visible(sequentialWorld, sequentialSystems, 1280.0f, 720.0f);
    assert_systems_visible(parallelWorld, parallelSystems, 1280.0f, 720.0f);

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
    assert(sequentialKilled == sequentialStars.targetStars);
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
