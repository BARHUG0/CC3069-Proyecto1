#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "systems.h"

/* World es SoA con arreglos en heap (ecs.h): no se puede memcmp la struct.
 * Se comparan los scalars y cada arreglo entrada por entrada hasta capacity. */
static void assert_worlds_equal(const World *a, const World *b)
{
    assert(a->capacity == b->capacity);
    assert(a->highWater == b->highWater);
    assert(a->alive == b->alive);
    assert(a->freeCount == b->freeCount);

    const uint32_t n = a->capacity;
    assert(memcmp(a->mask,     b->mask,     n * sizeof(*a->mask))     == 0);
    assert(memcmp(a->px,       b->px,       n * sizeof(*a->px))       == 0);
    assert(memcmp(a->py,       b->py,       n * sizeof(*a->py))       == 0);
    assert(memcmp(a->ocx,      b->ocx,      n * sizeof(*a->ocx))      == 0);
    assert(memcmp(a->ocy,      b->ocy,      n * sizeof(*a->ocy))      == 0);
    assert(memcmp(a->orx,      b->orx,      n * sizeof(*a->orx))      == 0);
    assert(memcmp(a->ory,      b->ory,      n * sizeof(*a->ory))      == 0);
    assert(memcmp(a->oang,     b->oang,     n * sizeof(*a->oang))     == 0);
    assert(memcmp(a->ospd,     b->ospd,     n * sizeof(*a->ospd))     == 0);
    assert(memcmp(a->rad,      b->rad,      n * sizeof(*a->rad))      == 0);
    assert(memcmp(a->alpha,    b->alpha,    n * sizeof(*a->alpha))    == 0);
    assert(memcmp(a->cr,       b->cr,       n * sizeof(*a->cr))       == 0);
    assert(memcmp(a->cg,       b->cg,       n * sizeof(*a->cg))       == 0);
    assert(memcmp(a->cb,       b->cb,       n * sizeof(*a->cb))       == 0);
    assert(memcmp(a->twPhase,  b->twPhase,  n * sizeof(*a->twPhase))  == 0);
    assert(memcmp(a->twFreq,   b->twFreq,   n * sizeof(*a->twFreq))   == 0);
    assert(memcmp(a->twBase,   b->twBase,   n * sizeof(*a->twBase))   == 0);
    assert(memcmp(a->twAmp,    b->twAmp,    n * sizeof(*a->twAmp))    == 0);
    assert(memcmp(a->life,     b->life,     n * sizeof(*a->life))     == 0);
    assert(memcmp(a->lifeMax,  b->lifeMax,  n * sizeof(*a->lifeMax))  == 0);
    assert(memcmp(a->freeList, b->freeList, n * sizeof(*a->freeList)) == 0);
}

/* SolarSystems y TrailBuffer tambien son SoA en heap: compara arreglo por
 * arreglo sobre las entradas validas. */
static void assert_systems_equal(const SolarSystems *a, const SolarSystems *b)
{
    assert(a->count == b->count);
    assert(a->ringTotal == b->ringTotal);
    assert(a->totalPlanets == b->totalPlanets);
    const int n = a->count, r = a->ringTotal;
    assert(memcmp(a->cx, b->cx, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->cy, b->cy, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->sun, b->sun, (size_t)n * sizeof(Entity)) == 0);
    assert(memcmp(a->planetCount, b->planetCount, (size_t)n * sizeof(int)) == 0);
    assert(memcmp(a->ringFirst, b->ringFirst, (size_t)n * sizeof(int)) == 0);
    assert(memcmp(a->anchor, b->anchor, (size_t)n * sizeof(int)) == 0);
    assert(memcmp(a->orbRad, b->orbRad, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->orbAng, b->orbAng, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->orbSpd, b->orbSpd, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->depth, b->depth, (size_t)n * sizeof(float)) == 0);
    assert(memcmp(a->ringCx, b->ringCx, (size_t)r * sizeof(float)) == 0);
    assert(memcmp(a->ringCy, b->ringCy, (size_t)r * sizeof(float)) == 0);
    assert(memcmp(a->ringRx, b->ringRx, (size_t)r * sizeof(float)) == 0);
    assert(memcmp(a->ringRy, b->ringRy, (size_t)r * sizeof(float)) == 0);
    assert(memcmp(a->ringEntity, b->ringEntity, (size_t)r * sizeof(Entity)) == 0);
}

static void assert_trails_equal(const TrailBuffer *a, const TrailBuffer *b)
{
    assert(a->maxBodies == b->maxBodies);
    assert(a->bodyCount == b->bodyCount);
    assert(a->head == b->head && a->fill == b->fill && a->accum == b->accum);
    const size_t nb = (size_t)a->bodyCount;
    const size_t samples = (size_t)TRAIL_LEN * (size_t)a->maxBodies;
    assert(memcmp(a->body, b->body, nb * sizeof(Entity)) == 0);
    assert(memcmp(a->cr, b->cr, nb) == 0);
    assert(memcmp(a->cg, b->cg, nb) == 0);
    assert(memcmp(a->cb, b->cb, nb) == 0);
    assert(memcmp(a->x, b->x, samples * sizeof(float)) == 0);
    assert(memcmp(a->y, b->y, samples * sizeof(float)) == 0);
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

/* Cada sistema tiene su PROPIA profundidad en [0,1] — nadie la comparte.
 * O(n^2), pero n <= MAX_SYSTEMS y solo en pruebas. */
static void assert_depths_unique(const SolarSystems *systems)
{
    for (int a = 0; a < systems->count; ++a) {
        assert(systems->depth[a] >= 0.0f && systems->depth[a] <= 1.0f);
        for (int b = a + 1; b < systems->count; ++b) {
            assert(systems->depth[a] != systems->depth[b]);
        }
    }
}

/* El reparto barajado mitad/mitad deja el conteo por ancla parejo. */
static void assert_anchor_balanced(const SolarSystems *systems)
{
    int n0 = 0, n1 = 0;
    for (int s = 0; s < systems->count; ++s) {
        if (systems->anchor[s] == 0) n0++; else n1++;
    }
    const int gap = n0 > n1 ? n0 - n1 : n1 - n0;
    assert(gap <= 1);
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
            solar_systems_reset(systems);
            spawn_solar_systems(world, systems, &rng, counts[count],
                                widths[size], heights[size]);
            assert_systems_orbit(world, systems);
            assert_depths_unique(systems);
            assert_anchor_balanced(systems);
        }
    }
}

/* Regresion del bug de la deriva: tras la reescritura "optimizacion" los
 * sistemas solo temblaban en su celda. Con el modelo de anclas restaurado,
 * en ~2 s de simulacion al menos un sistema debe recorrer una distancia
 * clara (el centro cx/cy, no un planeta). */
static void assert_drift_visible(World *world, SolarSystems *systems)
{
    Rng rng;
    rng_seed(&rng, 20260901u);
    ecs_reset(world);
    solar_systems_reset(systems);
    spawn_solar_systems(world, systems, &rng, 12, 1280.0f, 720.0f);

    float x0[64], y0[64];
    for (int s = 0; s < systems->count; ++s) {
        x0[s] = systems->cx[s];
        y0[s] = systems->cy[s];
    }

    for (int step = 0; step < 120; ++step) {
        sys_drift(world, systems, 1.0f / 60.0f);
    }

    float maxMoved = 0.0f;
    for (int s = 0; s < systems->count; ++s) {
        const float dx = systems->cx[s] - x0[s];
        const float dy = systems->cy[s] - y0[s];
        const float moved = sqrtf(dx * dx + dy * dy);
        if (moved > maxMoved) maxMoved = moved;
    }
    assert(maxMoved > 20.0f);
}

/* La profundidad sigue siendo unica tras varias muertes y renacimientos. */
static void assert_depth_survives_churn(World *world, SolarSystems *systems)
{
    Rng rng;
    rng_seed(&rng, 20260901u);
    ecs_reset(world);
    solar_systems_reset(systems);
    spawn_solar_systems(world, systems, &rng, 20, 1280.0f, 720.0f);

    for (int k = 0; k < 6; ++k) {
        solar_system_remove(world, systems, systems->count / 2);
    }
    assert_depths_unique(systems);

    for (int k = 0; k < 10; ++k) {
        assert(spawn_one_system(world, systems, &rng) >= 0);
    }
    assert_depths_unique(systems);
}

int main(void)
{
    World *sequentialWorld = ecs_world_alloc(0);
    World *parallelWorld = ecs_world_alloc(0);
    SolarSystems *sequentialSystems = solar_systems_alloc(512);
    SolarSystems *parallelSystems = solar_systems_alloc(512);
    TrailBuffer *sequentialTrails = trail_buffer_alloc(512 * MAX_PLANETS_PER_SYS);
    TrailBuffer *parallelTrails = trail_buffer_alloc(512 * MAX_PLANETS_PER_SYS);

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
    assert_systems_equal(sequentialSystems, parallelSystems);

    sys_drift(sequentialWorld, sequentialSystems, 0.016f);
    sys_drift(parallelWorld, parallelSystems, 0.016f);
    assert_worlds_equal(sequentialWorld, parallelWorld);
    assert_systems_equal(sequentialSystems, parallelSystems);
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
    assert_trails_equal(sequentialTrails, parallelTrails);
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
    assert_trails_equal(sequentialTrails, parallelTrails);
    assert_worlds_equal(sequentialWorld, parallelWorld);

    assert_layout_bounds(sequentialWorld, sequentialSystems);
    assert_drift_visible(sequentialWorld, sequentialSystems);
    assert_depth_survives_churn(sequentialWorld, sequentialSystems);

    trail_buffer_free(parallelTrails);
    trail_buffer_free(sequentialTrails);
    solar_systems_free(parallelSystems);
    solar_systems_free(sequentialSystems);
    ecs_world_free(parallelWorld);
    ecs_world_free(sequentialWorld);
    return 0;
}
