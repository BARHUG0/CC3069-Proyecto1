#include "spawn.h"

#include <math.h>

/* Paletas fijas (no hay razon para generar color libre: los colores estelares
 * reales caen en una banda estrecha del azul al ambar). */
static const uint8_t STAR_PALETTE[][3] = {
    { 255, 255, 255 }, /* blanca      */
    { 255, 255, 255 },
    { 202, 222, 255 }, /* azul-blanca */
    { 170, 200, 255 }, /* azul        */
    { 255, 226, 180 }, /* ambar       */
    { 255, 196, 170 }  /* rojiza      */
};
#define STAR_PALETTE_N (sizeof(STAR_PALETTE) / sizeof(STAR_PALETTE[0]))

static const uint8_t SUN_PALETTE[][3] = {
    { 255, 238, 170 },
    { 255, 214, 130 },
    { 255, 180, 110 },
    { 210, 228, 255 }
};
#define SUN_PALETTE_N (sizeof(SUN_PALETTE) / sizeof(SUN_PALETTE[0]))

static const uint8_t PLANET_PALETTE[][3] = {
    { 120, 190, 235 }, /* oceanico  */
    { 216, 160, 110 }, /* desertico */
    { 150, 210, 160 }, /* templado  */
    { 200, 200, 210 }, /* rocoso    */
    { 230, 130, 110 }, /* ferroso   */
    { 180, 150, 220 }, /* helado    */
    { 240, 220, 160 }  /* gaseoso   */
};
#define PLANET_PALETTE_N (sizeof(PLANET_PALETTE) / sizeof(PLANET_PALETTE[0]))

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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

static Entity spawn_sun(World *w, Rng *rng, float cx, float cy, float radius)
{
    Entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    const uint32_t p = rng_below(rng, (uint32_t)SUN_PALETTE_N);

    w->px[e]  = cx;
    w->py[e]  = cy;
    w->rad[e] = radius;
    w->cr[e]  = SUN_PALETTE[p][0];
    w->cg[e]  = SUN_PALETTE[p][1];
    w->cb[e]  = SUN_PALETTE[p][2];

    /* El sol reutiliza C_TWINKLE para latir: base alta y amplitud baja, asi el
     * mismo sistema de centelleo produce pulso solar sin codigo extra. */
    w->twFreq[e]  = rng_range(rng, 0.5f, 1.4f);
    w->twPhase[e] = rng_range(rng, 0.0f, 6.2831853f);
    w->twBase[e]  = 0.88f;
    w->twAmp[e]   = 0.12f;
    w->alpha[e]   = 1.0f;

    w->mask[e] = C_POS | C_RENDER | C_TWINKLE | C_SUN;
    return e;
}

static Entity spawn_planet(World *w, Rng *rng, float cx, float cy,
                           float rx, float ry, float speed, float radius)
{
    Entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    const uint32_t p = rng_below(rng, (uint32_t)PLANET_PALETTE_N);

    w->ocx[e]  = cx;
    w->ocy[e]  = cy;
    w->orx[e]  = rx;
    w->ory[e]  = ry;
    w->oang[e] = rng_range(rng, 0.0f, 6.2831853f);
    w->ospd[e] = speed;

    w->px[e] = cx + rx * cosf(w->oang[e]);
    w->py[e] = cy + ry * sinf(w->oang[e]);

    w->rad[e]   = radius;
    w->alpha[e] = 1.0f;
    w->cr[e]    = PLANET_PALETTE[p][0];
    w->cg[e]    = PLANET_PALETTE[p][1];
    w->cb[e]    = PLANET_PALETTE[p][2];

    w->mask[e] = C_POS | C_ORBIT | C_RENDER;
    return e;
}

void spawn_solar_systems(World *w, SolarSystems *ss, Rng *rng,
                         int n, float screenW, float screenH)
{
    if (n > MAX_SYSTEMS) n = MAX_SYSTEMS;
    if (n < 1)           n = 1;

    ss->count        = n;
    ss->ringTotal    = 0;
    ss->totalPlanets = 0;

    /* Rejilla lo mas cuadrada posible para repartir los sistemas por la
     * pantalla sin que las orbitas se solapen. */
    const int cols = (int)ceilf(sqrtf((float)n));
    const int rows = (int)ceilf((float)n / (float)cols);

    const float cellW = screenW / (float)cols;
    const float cellH = screenH / (float)rows;

    /* 0.42 deja un margen entre celdas vecinas para que los anillos exteriores
     * de dos sistemas no se toquen. */
    const float cellR = 0.42f * ((cellW < cellH) ? cellW : cellH);

    const float sunRad    = clampf(cellR * 0.16f, 2.5f, 10.0f);
    const float planetRef = clampf(cellR * 0.055f, 1.2f, 5.0f);

    for (int s = 0; s < n; ++s) {
        const int col = s % cols;
        const int row = s / cols;

        /* Jitter para que la rejilla no se note. */
        const float jx = rng_range(rng, -0.12f, 0.12f) * cellW;
        const float jy = rng_range(rng, -0.12f, 0.12f) * cellH;

        const float cx = ((float)col + 0.5f) * cellW + jx;
        const float cy = ((float)row + 0.5f) * cellH + jy;

        ss->cx[s]        = cx;
        ss->cy[s]        = cy;
        ss->ringFirst[s] = ss->ringTotal;
        ss->sun[s]       = spawn_sun(w, rng, cx, cy, sunRad);

        const int planets = MIN_PLANETS +
            (int)rng_below(rng, (uint32_t)(MAX_PLANETS_PER_SYS - MIN_PLANETS + 1));

        /* Velocidad de referencia del sistema: cada sistema gira a su ritmo. */
        const float baseSpeed = rng_range(rng, 0.35f, 0.85f);
        const float spin      = rng_sign(rng); /* algunos sistemas retrogrados */

        int created = 0;
        for (int i = 0; i < planets; ++i) {
            if (ss->ringTotal >= MAX_PLANETS_TOTAL) {
                break;
            }

            /* Radios escalonados de 0.26 a 1.0 del radio de celda. */
            const float frac = 0.26f + 0.74f * ((float)(i + 1) / (float)planets)
                                     + rng_range(rng, -0.025f, 0.025f);
            float rx = cellR * frac;
            float ry = rx * rng_range(rng, 0.45f, 1.0f); /* elipse achatada */

            const float pr = planetRef * rng_range(rng, 0.65f, 1.35f);

            /* Que el planeta no quede dentro del sol. */
            const float minR = sunRad + pr + 3.0f;
            if (rx < minR) rx = minR;
            if (ry < minR) ry = minR;

            /* Tercera ley de Kepler aproximada: T^2 ~ a^3, luego w ~ a^-1.5.
             * Los planetas interiores giran mas rapido que los exteriores. */
            float speed = baseSpeed * powf(cellR / rx, 1.5f);
            speed = clampf(speed, 0.05f, 3.0f) * spin;

            const Entity pe = spawn_planet(w, rng, cx, cy, rx, ry, speed, pr);
            if (pe == ECS_INVALID) {
                break; /* mundo lleno */
            }

            ss->ringCx[ss->ringTotal] = cx;
            ss->ringCy[ss->ringTotal] = cy;
            ss->ringRx[ss->ringTotal] = rx;
            ss->ringRy[ss->ringTotal] = ry;
            ss->ringTotal++;
            created++;
        }

        ss->planetCount[s] = created;
        ss->totalPlanets  += created;
    }
}
