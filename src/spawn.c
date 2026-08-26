#include "spawn.h"

#include <math.h>

/* Paletas fijas (no hay razon para generar color libre: los colores estelares
 * reales caen en una banda estrecha del azul al ambar).
 *
 * Las estrellas de fondo y los soles de los sistemas son ambos "estrellas" en
 * la realidad, pero aqui cumplen roles visuales distintos (fondo lejano vs.
 * protagonista de un sistema), asi que se separan en bandas de color que NO
 * se solapan: el fondo se queda en el extremo frio (tipos O/B/A, azul-blanco,
 * los mas comunes a simple vista por su brillo) y los soles en el extremo
 * calido (tipos G/K/M, como el propio Sol, que es lo que suele tener planetas
 * alrededor). Sin esa separacion, un sol y una estrella de fondo del mismo
 * tono blanco-amarillo se confunden en pantalla. */
static const uint8_t STAR_PALETTE[][3] = {
    { 255, 255, 255 }, /* blanca (tipo A)       */
    { 255, 255, 255 },
    { 225, 235, 255 }, /* blanca-azulada (F/A)  */
    { 202, 222, 255 }, /* azul-blanca (B)       */
    { 170, 200, 255 }  /* azul (O)              */
};
#define STAR_PALETTE_N (sizeof(STAR_PALETTE) / sizeof(STAR_PALETTE[0]))

static const uint8_t SUN_PALETTE[][3] = {
    { 255, 244, 200 }, /* tipo G, como el Sol   */
    { 255, 214, 130 }, /* tipo K, naranja       */
    { 255, 176, 110 }, /* K/M, naranja-rojizo   */
    { 255, 140, 100 }  /* tipo M, enana roja    */
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

    /* Dos anclas fijas, una por mitad de pantalla. */
    ss->anchorX[0] = screenW * 0.25f;
    ss->anchorY[0] = screenH * 0.5f;
    ss->anchorX[1] = screenW * 0.75f;
    ss->anchorY[1] = screenH * 0.5f;

    /* Jitter chico para que la rejilla de posiciones no se note pero los
     * sistemas no se toquen al nacer. */
    const float jitter = 0.12f * ((cellW < cellH) ? cellW : cellH);

    /* Reparto barajado, no un volado independiente por sistema: con N chico
     * (el caso tipico) unos cuantos volados de moneda pueden caer 8 a 2 por
     * puro azar, y eso se lee en pantalla como "sigue pegado a un lado"
     * aunque sea random de verdad. Se arma n/2 y n/2 (el impar va al lado 1)
     * y se baraja con Fisher-Yates: el orden es al azar, el conteo por lado
     * siempre queda parejo. Barajar el ancla (y no la posicion, que sigue
     * fija por col/row) es lo que garantiza que a que mitad orbita un
     * sistema no tenga relacion con en que mitad nacio. */
    int assign[MAX_SYSTEMS];
    for (int s = 0; s < n; ++s) {
        assign[s] = (s < n / 2) ? 0 : 1;
    }
    for (int s = n - 1; s > 0; --s) {
        const int j = (int)rng_below(rng, (uint32_t)(s + 1));
        const int tmp = assign[s];
        assign[s] = assign[j];
        assign[j] = tmp;
    }

    for (int s = 0; s < n; ++s) {
        const int anchorIdx = assign[s];
        ss->anchor[s] = anchorIdx;

        /* Posicion propia del sistema: rejilla + jitter, sin mirar el ancla
         * que le toco. Puede caer en cualquier mitad de pantalla. */
        const int col = s % cols;
        const int row = s / cols;
        const float cx = (col + 0.5f) * cellW + rng_range(rng, -jitter, jitter);
        const float cy = (row + 0.5f) * cellH + rng_range(rng, -jitter, jitter);

        /* Radio y angulo de orbita: geometria real hacia el ancla sorteada,
         * no un sorteo acotado a la mitad del ancla. Un sistema nacido en una
         * mitad orbitando el ancla de la otra barre ambas. */
        const float dx = cx - ss->anchorX[anchorIdx];
        const float dy = cy - ss->anchorY[anchorIdx];
        const float radius = sqrtf(dx * dx + dy * dy);
        const float angle  = atan2f(dy, dx);

        /* Velocidad angular derivada de una velocidad lineal fija (~25-70
         * px/s), no un rad/s fijo: con radios que ahora varian mucho (~0 a
         * ~1000px), un rad/s fijo haria que los sistemas de radio grande
         * volaran por la pantalla. */
        const float v = rng_range(rng, 25.0f, 70.0f);
        float ospd = (radius > 1.0f) ? v / radius : 0.3f;
        ospd = rng_sign(rng) * clampf(ospd, 0.03f, 0.5f);

        ss->orbRad[s] = radius;
        ss->orbAng[s] = angle;
        ss->orbSpd[s] = ospd;

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

            ss->ringCx[ss->ringTotal]     = cx;
            ss->ringCy[ss->ringTotal]     = cy;
            ss->ringRx[ss->ringTotal]     = rx;
            ss->ringRy[ss->ringTotal]     = ry;
            ss->ringEntity[ss->ringTotal] = pe;
            ss->ringTotal++;
            created++;
        }

        ss->planetCount[s]  = created;
        ss->totalPlanets   += created;
    }
}
