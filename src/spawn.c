#include "spawn.h"

#include <math.h>
#include <string.h>

/* Paletas fijas (no hay razon para generar color libre: los colores estelares
 * reales caen en una banda estrecha del azul al ambar). Retunadas para verse
 * mas vividas/saturadas que la version original (menos pastel), sin tocar la
 * regla de abajo.
 *
 * Las estrellas de fondo y los soles de los sistemas son ambos "estrellas" en
 * la realidad, pero aqui cumplen roles visuales distintos (fondo lejano vs.
 * protagonista de un sistema), asi que se separan en bandas de color que NO
 * se solapan: el fondo se queda en el extremo frio (tipos O/B/A, azul-blanco,
 * los mas comunes a simple vista por su brillo) y los soles en el extremo
 * calido (tipos G/K/M, como el propio Sol, que es lo que suele tener planetas
 * alrededor). Sin esa separacion, un sol y una estrella de fondo del mismo
 * tono blanco-amarillo se confunden en pantalla. Subir la saturacion mueve
 * las estrellas de fondo mas hacia el azul y los soles mas hacia el naranja,
 * o sea AUMENTA la separacion entre bandas — la regla no corre riesgo con
 * este retune; solo se rompe si se agrega amarillo al fondo o blanco a los
 * soles. */
static const uint8_t STAR_PALETTE[][3] = {
    { 255, 255, 255 }, /* blanca (tipo A)       */
    { 255, 255, 255 }, /* duplicada a proposito: pesa 2/5 la blanca pura */
    { 215, 230, 255 }, /* blanca-azulada (F/A)  */
    { 170, 205, 255 }, /* azul-blanca (B)       */
    { 110, 170, 255 }  /* azul (O)              */
};
#define STAR_PALETTE_N (sizeof(STAR_PALETTE) / sizeof(STAR_PALETTE[0]))

/* Ordenada frio->calido a proposito: el indice ES el ordinal de calidez que
 * usa spawn_system_into_slot para decidir el tono de los planetas (ver
 * spawn_planet mas abajo) — asi ese enlace sobrevive a un futuro retune de
 * esta tabla, en vez de un umbral de RGB ajustado a mano por separado que
 * podria desincronizarse (mismo motivo por el que ds->frontEaseDeg en
 * deathstar.c ya no es una constante suelta). */
static const uint8_t SUN_PALETTE[][3] = {
    { 255, 232, 150 }, /* tipo G, como el Sol   */
    { 255, 190,  95 }, /* tipo K, naranja       */
    { 255, 150,  80 }, /* K/M, naranja-rojizo   */
    { 255, 100,  70 }  /* tipo M, enana roja    */
};
#define SUN_PALETTE_N (sizeof(SUN_PALETTE) / sizeof(SUN_PALETTE[0]))

/* Ordenada fria->calida, con dos rangos superpuestos (no dos tablas): un sol
 * frio (mitad fria de SUN_PALETTE) tira los planetas al rango
 * [PLANET_COOL_LO,PLANET_COOL_HI), uno calido al rango
 * [PLANET_WARM_LO,PLANET_WARM_HI) — templado/rocoso caen en ambos por ser
 * razonablemente neutros, para que ningun sistema se vea monocromo. Motivo
 * fisico, no solo estetico: la luz reflejada por un planeta esta tenida por
 * la de su sol, asi que sol calido -> planetas calidos es lo esperable (la
 * intuicion ingenua es al reves). */
static const uint8_t PLANET_PALETTE[][3] = {
    {  70, 170, 235 }, /* oceanico  */
    { 150, 110, 230 }, /* helado    */
    { 110, 215, 130 }, /* templado  */
    { 200, 195, 190 }, /* rocoso    */
    { 230, 145,  80 }, /* desertico */
    { 235,  95,  70 }, /* ferroso   */
    { 245, 200, 110 }  /* gaseoso   */
};
#define PLANET_COOL_LO 0u /* oceanico..rocoso            */
#define PLANET_COOL_HI 4u
#define PLANET_WARM_LO 2u /* templado..gaseoso           */
#define PLANET_WARM_HI 7u

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

static Entity spawn_sun(World *w, Rng *rng, float cx, float cy, float radius, uint32_t pal)
{
    Entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    w->px[e]  = cx;
    w->py[e]  = cy;
    w->rad[e] = radius;
    w->cr[e]  = SUN_PALETTE[pal][0];
    w->cg[e]  = SUN_PALETTE[pal][1];
    w->cb[e]  = SUN_PALETTE[pal][2];

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
                           float rx, float ry, float speed, float radius, int warm)
{
    Entity e = ecs_create(w);
    if (e == ECS_INVALID) {
        return ECS_INVALID;
    }

    /* Rango de la paleta segun la calidez del sol del sistema (ver el
     * comentario de PLANET_PALETTE arriba) — no toda la tabla. */
    const uint32_t lo = warm ? PLANET_WARM_LO : PLANET_COOL_LO;
    const uint32_t hi = warm ? PLANET_WARM_HI : PLANET_COOL_HI;
    const uint32_t p  = lo + rng_below(rng, hi - lo);

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

/* `layer` (ver SYS_LAYER_COUNT, spawn.h) hornea la profundidad en la
 * GEOMETRIA una sola vez aca (radio del sol, radios de orbita, velocidad de
 * deriva) — el brillo/alpha por capa vive en el render (systems.c), no aca,
 * para no pelear con sys_twinkle que reescribe w->alpha[e] cada frame para
 * el sol. IMPORTANTE: usa copias LOCALES escaladas (cellRs/sunRadS/prRef),
 * nunca ss->cellR/ss->sunRad directamente — esos son la plantilla
 * compartida que reusan spawn_one_system (cada respawn futuro) y
 * deathstar_update (ds->blastR); escalarlos in-place corromperia a todos
 * los sistemas siguientes. */
static void spawn_system_into_slot(World *w, SolarSystems *ss, Rng *rng,
                                   int s, float homeX, float homeY, int layer)
{
    ss->layer[s]  = layer;
    const float sc = solar_layer_scale(layer);

    ss->homeX[s]  = homeX;
    ss->homeY[s]  = homeY;
    ss->orbRad[s] = rng_range(rng, ss->driftMax * 0.35f, ss->driftMax);
    ss->orbAng[s] = rng_range(rng, 0.0f, 6.2831853f);
    ss->orbSpd[s] = rng_sign(rng) * rng_range(rng, 0.08f, 0.22f) * sc;

    const float cx = homeX + ss->orbRad[s] * cosf(ss->orbAng[s]);
    const float cy = homeY + ss->orbRad[s] * sinf(ss->orbAng[s]);
    ss->cx[s] = cx;
    ss->cy[s] = cy;
    ss->ringFirst[s] = ss->ringTotal;

    /* Copias locales escaladas por capa — ver el comentario de la funcion. */
    const float cellRs  = ss->cellR * sc;
    const float sunRadS = ss->sunRad * sc;
    const float prRef   = ss->planetRef * sc;

    /* El indice de SUN_PALETTE se saca ANTES de llamar a spawn_sun (mismo
     * orden de sorteo de RNG que antes, spawn_sun lo dibujaba como su primer
     * numero) para poder derivar la calidez del sistema: mitad fria de la
     * tabla (indices 0..N/2) -> planetas frios, mitad calida -> planetas
     * calidos. Ver el comentario de PLANET_PALETTE. */
    const uint32_t sunPal = rng_below(rng, (uint32_t)SUN_PALETTE_N);
    const int      warm   = (sunPal >= SUN_PALETTE_N / 2);
    ss->sun[s] = spawn_sun(w, rng, cx, cy, sunRadS, sunPal);

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

        /* Radios escalonados de 0.26 a 1.0 del radio de celda (ya escalado
         * por capa). */
        const float frac = 0.26f + 0.74f * ((float)(i + 1) / (float)planets)
                                 + rng_range(rng, -0.025f, 0.025f);
        float rx = cellRs * frac;
        float ry = rx * rng_range(rng, 0.45f, 1.0f); /* elipse achatada */

        const float pr = prRef * rng_range(rng, 0.65f, 1.35f);

        const float gap = fminf(3.0f * sc, cellRs * 0.10f);
        const float minR = sunRadS + pr + gap;
        if (rx < minR) rx = minR;
        if (ry < minR) ry = minR;

        /* Tercera ley de Kepler aproximada: T^2 ~ a^3, luego w ~ a^-1.5.
         * Los planetas interiores giran mas rapido que los exteriores.
         * cellRs, no ss->cellR: los dos lados de la razon deben estar en la
         * misma escala, si no los sistemas de atras girarian MAS rapido
         * (proporcion invertida), justo al reves de lo que se quiere. */
        float speed = baseSpeed * powf(cellRs / rx, 1.5f);
        speed = clampf(speed, 0.05f, 3.0f) * spin;

        const Entity pe = spawn_planet(w, rng, cx, cy, rx, ry, speed, pr, warm);
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

    ss->gridCols = cols;
    ss->gridRows = rows;
    ss->cellW    = screenW / (float)cols;
    ss->cellH    = screenH / (float)rows;

    const float cellMin = (ss->cellW < ss->cellH) ? ss->cellW : ss->cellH;
    ss->cellR = 0.40f * cellMin;

    ss->sunRad    = clampf(ss->cellR * 0.16f, 0.8f, 10.0f);
    ss->planetRef = clampf(ss->cellR * 0.055f, 0.45f, 5.0f);
    ss->driftMax = 0.04f * cellMin;

    int lay[MAX_SYSTEMS];
    for (int s = 0; s < n; ++s) {
        lay[s] = s % SYS_LAYER_COUNT;
    }
    for (int s = n - 1; s > 0; --s) {
        const int j = (int)rng_below(rng, (uint32_t)(s + 1));
        const int tmp = lay[s];
        lay[s] = lay[j];
        lay[j] = tmp;
    }

    for (int s = 0; s < n; ++s) {
        const int col = s % cols;
        const int row = s / cols;
        const float homeX = (col + 0.5f) * ss->cellW;
        const float homeY = (row + 0.5f) * ss->cellH;

        spawn_system_into_slot(w, ss, rng, s, homeX, homeY, lay[s]);
    }
}

int spawn_one_system(World *w, SolarSystems *ss, Rng *rng)
{
    if (ss->count >= MAX_SYSTEMS) {
        return -1;
    }

    int layCount[SYS_LAYER_COUNT] = { 0 };
    for (int i = 0; i < ss->count; ++i) {
        layCount[ss->layer[i]]++;
    }
    int layer = 0;
    for (int L = 1; L < SYS_LAYER_COUNT; ++L) {
        if (layCount[L] < layCount[layer]) {
            layer = L;
        }
    }

    /* Celda al azar de la misma rejilla del spawn inicial. Solaparse con un
     * sistema vivo es aceptado a proposito, igual que en el spawn inicial. */
    const int col = (int)rng_below(rng, (uint32_t)ss->gridCols);
    const int row = (int)rng_below(rng, (uint32_t)ss->gridRows);
    const float homeX = (col + 0.5f) * ss->cellW;
    const float homeY = (row + 0.5f) * ss->cellH;

    const int s = ss->count;
    spawn_system_into_slot(w, ss, rng, s, homeX, homeY, layer);
    ss->count++;
    return s;
}

void solar_system_remove(World *w, SolarSystems *ss, int s)
{
    if (s < 0 || s >= ss->count) {
        return;
    }

    const int first = ss->ringFirst[s];
    const int cnt   = ss->planetCount[s];

    if (ss->sun[s] != ECS_INVALID) {
        ecs_destroy(w, ss->sun[s]);
    }
    for (int i = first; i < first + cnt; ++i) {
        ecs_destroy(w, ss->ringEntity[i]);
    }

    /* Compactar la tabla de anillos: sin esto ringTotal nunca recicla y tras
     * unos cientos de muertes topa en MAX_PLANETS_TOTAL, dejando de nacer
     * planetas nuevos. */
    const int tail = ss->ringTotal - (first + cnt);
    if (tail > 0) {
        memmove(&ss->ringCx[first],     &ss->ringCx[first + cnt],     (size_t)tail * sizeof(float));
        memmove(&ss->ringCy[first],     &ss->ringCy[first + cnt],     (size_t)tail * sizeof(float));
        memmove(&ss->ringRx[first],     &ss->ringRx[first + cnt],     (size_t)tail * sizeof(float));
        memmove(&ss->ringRy[first],     &ss->ringRy[first + cnt],     (size_t)tail * sizeof(float));
        memmove(&ss->ringEntity[first], &ss->ringEntity[first + cnt], (size_t)tail * sizeof(Entity));
    }
    ss->ringTotal    -= cnt;
    ss->totalPlanets -= cnt;

    for (int t = 0; t < ss->count; ++t) {
        if (ss->ringFirst[t] > first) {
            ss->ringFirst[t] -= cnt;
        }
    }

    /* Swap-remove del slot s con el ultimo sistema. */
    const int last = ss->count - 1;
    if (s != last) {
        ss->cx[s]          = ss->cx[last];
        ss->cy[s]          = ss->cy[last];
        ss->sun[s]         = ss->sun[last];
        ss->planetCount[s] = ss->planetCount[last];
        ss->ringFirst[s]   = ss->ringFirst[last];
        ss->homeX[s]       = ss->homeX[last];
        ss->homeY[s]       = ss->homeY[last];
        ss->orbRad[s]      = ss->orbRad[last];
        ss->orbAng[s]      = ss->orbAng[last];
        ss->orbSpd[s]      = ss->orbSpd[last];
        ss->layer[s]       = ss->layer[last];
    }
    ss->count--;
}
