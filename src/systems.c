#include "systems.h"

#include <math.h>

#include "raylib.h"

#define FADE_IN_SECS  0.8f
#define FADE_OUT_SECS 1.2f
#define C_PENDING_DESTROY (1u << 31)

#ifdef _OPENMP
#define OMP_PARALLEL_FOR _Pragma("omp parallel for schedule(static)")
#else
#define OMP_PARALLEL_FOR
#endif

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static unsigned char alpha8(float a)
{
    const float v = clamp01(a) * 255.0f;
    return (unsigned char)v;
}

void starfield_init(StarField *sf, int targetStars, float screenW, float screenH)
{
    sf->accumulator = 0.0f;
    sf->targetStars = targetStars;
    sf->liveStars   = 0;
    sf->screenW     = screenW;
    sf->screenH     = screenH;

    /* Vida media de una estrella ~9 s, asi que para sostener targetStars vivas
     * hay que reponer targetStars/9 por segundo. El tope de poblacion hace el
     * resto: al alcanzarlo el sistema deja de crear. */
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

    /* Tope por fotograma proporcional al objetivo. Un tope fijo (64) haria
     * inalcanzable cualquier poblacion grande: con dt alto la reposicion pedida
     * supera el tope y las bajas ganan, estancando el cielo muy por debajo de
     * targetStars. El limite real de suavidad ya lo pone rate*dt. */
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

int sys_spawn_stars_parallel(World *w, StarField *sf, Rng *rng, float dt)
{
    return sys_spawn_stars(w, sf, rng, dt);
}

static void twinkle_entity(World *w, uint32_t e, float t)
{
    const uint32_t want = C_RENDER | C_TWINKLE;
    if ((w->mask[e] & want) != want) {
        return;
    }
    w->alpha[e] = clamp01(w->twBase[e] +
                          w->twAmp[e] * sinf(w->twFreq[e] * t + w->twPhase[e]));
}

void sys_twinkle(World *w, float t)
{
    const uint32_t n = w->highWater;

    for (uint32_t e = 0; e < n; ++e) {
        twinkle_entity(w, e, t);
    }
}

void sys_twinkle_parallel(World *w, float t)
{
    const uint32_t n = w->highWater;

    OMP_PARALLEL_FOR
    for (uint32_t e = 0; e < n; ++e) {
        twinkle_entity(w, e, t);
    }
}

static void drift_system(World *w, SolarSystems *ss, int s, float dt)
{
    float a = ss->orbAng[s] + ss->orbSpd[s] * dt;
    if (a > 6.2831853f) a -= 6.2831853f;
    if (a < 0.0f)       a += 6.2831853f;
    ss->orbAng[s] = a;

    const int   anchorIdx = ss->anchor[s];
    const float cx = ss->anchorX[anchorIdx] + ss->orbRad[s] * cosf(a);
    const float cy = ss->anchorY[anchorIdx] + ss->orbRad[s] * sinf(a);

    ss->cx[s] = cx;
    ss->cy[s] = cy;

    if (ss->sun[s] != ECS_INVALID) {
        w->px[ss->sun[s]] = cx;
        w->py[ss->sun[s]] = cy;
    }

    const int first = ss->ringFirst[s];
    const int last  = first + ss->planetCount[s];
    for (int i = first; i < last; ++i) {
        ss->ringCx[i] = cx;
        ss->ringCy[i] = cy;

        const Entity pe = ss->ringEntity[i];
        w->ocx[pe] = cx;
        w->ocy[pe] = cy;
    }
}

void sys_drift(World *w, SolarSystems *ss, float dt)
{
    for (int s = 0; s < ss->count; ++s) {
        drift_system(w, ss, s, dt);
    }
}

void sys_drift_parallel(World *w, SolarSystems *ss, float dt)
{
    OMP_PARALLEL_FOR
    for (int s = 0; s < ss->count; ++s) {
        drift_system(w, ss, s, dt);
    }
}

static void orbit_entity(World *w, uint32_t e, float dt)
{
    const uint32_t want = C_ORBIT | C_POS;
    if ((w->mask[e] & want) != want) {
        return;
    }
    float a = w->oang[e] + w->ospd[e] * dt;

    if (a > 6.2831853f)  a -= 6.2831853f;
    if (a < 0.0f)        a += 6.2831853f;

    w->oang[e] = a;
    w->px[e]   = w->ocx[e] + w->orx[e] * cosf(a);
    w->py[e]   = w->ocy[e] + w->ory[e] * sinf(a);
}

void sys_orbit(World *w, float dt)
{
    const uint32_t n = w->highWater;

    for (uint32_t e = 0; e < n; ++e) {
        orbit_entity(w, e, dt);
    }
}

void sys_orbit_parallel(World *w, float dt)
{
    const uint32_t n = w->highWater;

    OMP_PARALLEL_FOR
    for (uint32_t e = 0; e < n; ++e) {
        orbit_entity(w, e, dt);
    }
}

static int lifetime_entity(World *w, uint32_t e, float dt)
{
    if ((w->mask[e] & C_LIFE) == 0u) {
        return 0;
    }

    const float life = w->life[e] - dt;
    if (life <= 0.0f) {
        return 1;
    }
    w->life[e] = life;

    const float age = w->lifeMax[e] - life;
    float env = 1.0f;
    if (age < FADE_IN_SECS)   env  = age / FADE_IN_SECS;
    if (life < FADE_OUT_SECS) env *= life / FADE_OUT_SECS;

    w->alpha[e] *= env;
    return 0;
}

int sys_lifetime(World *w, float dt)
{
    const uint32_t n = w->highWater;
    int killed = 0;

    for (uint32_t e = 0; e < n; ++e) {
        if (lifetime_entity(w, e, dt)) {
            ecs_destroy(w, e);
            killed++;
        }
    }
    return killed;
}

int sys_lifetime_parallel(World *w, float dt)
{
    const uint32_t n = w->highWater;

    OMP_PARALLEL_FOR
    for (uint32_t e = 0; e < n; ++e) {
        if (lifetime_entity(w, e, dt)) {
            w->mask[e] |= C_PENDING_DESTROY;
        }
    }

    int killed = 0;
    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & C_PENDING_DESTROY) != 0u) {
            ecs_destroy(w, e);
            killed++;
        }
    }
    return killed;
}

/* --- estelas -------------------------------------------------------------- */

/* Agrega el cuerpo e al final de la tabla, con su color atenuado por mul
 * (ver solar_layer_alpha, spawn.h — asi la estela de un sistema de atras no
 * queda mas brillante que su propio sol/planeta ya atenuados). Siembra las
 * TRAIL_LEN muestras con la posicion actual: la estela nueva arranca como
 * un punto en vez de con basura del slot que ocupara antes (head/fill son
 * un eje de tiempo COMPARTIDO por todos los cuerpos). */
static void trails_append_body(TrailBuffer *tb, const World *w, Entity e, float mul)
{
    if (tb->bodyCount >= MAX_TRAIL_BODIES) {
        return;
    }
    const int b = tb->bodyCount++;
    tb->body[b] = e;
    tb->cr[b]   = (uint8_t)((float)w->cr[e] * mul);
    tb->cg[b]   = (uint8_t)((float)w->cg[e] * mul);
    tb->cb[b]   = (uint8_t)((float)w->cb[e] * mul);
    for (int k = 0; k < TRAIL_LEN; ++k) {
        tb->x[k][b] = w->px[e];
        tb->y[k][b] = w->py[e];
    }
}

/* Recorre los sistemas uno por uno (sol, luego sus planetas via
 * ringFirst[s]/planetCount[s]) en vez de un escaneo plano de ss->ringTotal:
 * asi cada cuerpo se agrega con el mul de SU sistema. */
void trails_init(TrailBuffer *tb, const World *w, const SolarSystems *ss)
{
    tb->bodyCount = 0;
    for (int s = 0; s < ss->count; ++s) {
        const float mul = solar_layer_alpha(ss->layer[s]);
        if (ss->sun[s] != ECS_INVALID) {
            trails_append_body(tb, w, ss->sun[s], mul);
        }
        const int first = ss->ringFirst[s];
        const int last  = first + ss->planetCount[s];
        for (int i = first; i < last; ++i) {
            trails_append_body(tb, w, ss->ringEntity[i], mul);
        }
    }

    tb->head  = 0;
    tb->fill  = 0;
    tb->accum = 0.0f;
}

void sys_trails(const World *w, TrailBuffer *tb, float dt)
{
    tb->accum += dt;

    const float period = 1.0f / TRAIL_HZ;
    if (tb->accum < period) {
        return;
    }
    tb->accum -= period;

    const int head = tb->head;
    for (int b = 0; b < tb->bodyCount; ++b) {
        const Entity e = tb->body[b];
        tb->x[head][b] = w->px[e];
        tb->y[head][b] = w->py[e];
    }

    tb->head = (head + 1) % TRAIL_LEN;
    if (tb->fill < TRAIL_LEN) {
        tb->fill++;
    }
}

void sys_trails_parallel(const World *w, TrailBuffer *tb, float dt)
{
    tb->accum += dt;

    const float period = 1.0f / TRAIL_HZ;
    if (tb->accum < period) {
        return;
    }
    tb->accum -= period;

    const int head = tb->head;
    OMP_PARALLEL_FOR
    for (int b = 0; b < tb->bodyCount; ++b) {
        const Entity e = tb->body[b];
        tb->x[head][b] = w->px[e];
        tb->y[head][b] = w->py[e];
    }

    tb->head = (head + 1) % TRAIL_LEN;
    if (tb->fill < TRAIL_LEN) {
        tb->fill++;
    }
}

/* Swap-remove de la columna b: copia la ultima columna encima y acorta
 * bodyCount. Recorre las TRAIL_LEN filas, no solo [oldest,head): el layout es
 * [muestra][cuerpo], asi que mover una columna entera es un salto de tamano
 * bodyCount por fila. */
static void trails_drop_column(TrailBuffer *tb, int b)
{
    const int last = tb->bodyCount - 1;
    if (b != last) {
        for (int k = 0; k < TRAIL_LEN; ++k) {
            tb->x[k][b] = tb->x[k][last];
            tb->y[k][b] = tb->y[k][last];
        }
        tb->body[b] = tb->body[last];
        tb->cr[b]   = tb->cr[last];
        tb->cg[b]   = tb->cg[last];
        tb->cb[b]   = tb->cb[last];
    }
    tb->bodyCount--;
}

static int trails_find_body(const TrailBuffer *tb, Entity e)
{
    for (int b = 0; b < tb->bodyCount; ++b) {
        if (tb->body[b] == e) {
            return b;
        }
    }
    return -1;
}

void trails_drop_system(TrailBuffer *tb, const SolarSystems *ss, int s)
{
    if (ss->sun[s] != ECS_INVALID) {
        const int b = trails_find_body(tb, ss->sun[s]);
        if (b >= 0) {
            trails_drop_column(tb, b);
        }
    }
    const int first = ss->ringFirst[s];
    const int last  = first + ss->planetCount[s];
    for (int i = first; i < last; ++i) {
        const int b = trails_find_body(tb, ss->ringEntity[i]);
        if (b >= 0) {
            trails_drop_column(tb, b);
        }
    }
}

void trails_add_system(TrailBuffer *tb, const World *w, const SolarSystems *ss, int s)
{
    const float mul = solar_layer_alpha(ss->layer[s]);
    if (ss->sun[s] != ECS_INVALID) {
        trails_append_body(tb, w, ss->sun[s], mul);
    }
    const int first = ss->ringFirst[s];
    const int last  = first + ss->planetCount[s];
    for (int i = first; i < last; ++i) {
        trails_append_body(tb, w, ss->ringEntity[i], mul);
    }
}

/* --- render ------------------------------------------------------------- */

static void render_starfield(const World *w)
{
    const uint32_t n = w->highWater;
    /* Estrella de fondo = se pinta y centellea, pero NO es un sol. */
    const uint32_t want = C_RENDER | C_TWINKLE;

    BeginBlendMode(BLEND_ADDITIVE);
    for (uint32_t e = 0; e < n; ++e) {
        const uint32_t m = w->mask[e];
        if ((m & want) != want || (m & C_SUN) != 0u) {
            continue;
        }
        const float a = w->alpha[e];
        if (a <= 0.01f) {
            continue;
        }

        const Vector2 p = { w->px[e], w->py[e] };
        const float   r = w->rad[e];

        if (r > 1.35f) {
            /* Halo y cruz de difraccion solo en las mas brillantes: es lo que
             * da la sensacion de "destello" sin costar 4 primitivas por
             * estrella en las 1500 del fondo. */
            DrawCircleV(p, r * 2.7f, (Color){ w->cr[e], w->cg[e], w->cb[e], alpha8(a * 0.18f) });

            const float len   = r * 3.6f;
            const Color spike = { w->cr[e], w->cg[e], w->cb[e], alpha8(a * 0.28f) };
            DrawLineEx((Vector2){ p.x - len, p.y }, (Vector2){ p.x + len, p.y }, 1.0f, spike);
            DrawLineEx((Vector2){ p.x, p.y - len }, (Vector2){ p.x, p.y + len }, 1.0f, spike);
        }

        DrawCircleV(p, r, (Color){ w->cr[e], w->cg[e], w->cb[e], alpha8(a) });
    }
    EndBlendMode();
}

/* Por sistema (no un escaneo plano de ringTotal): asi el alpha del anillo
 * puede atenuarse por la capa de profundidad de SU sistema. */
static void render_rings(const SolarSystems *ss)
{
    for (int s = 0; s < ss->count; ++s) {
        const unsigned char a = alpha8(30.0f / 255.0f * solar_layer_alpha(ss->layer[s]));
        const int first = ss->ringFirst[s];
        const int last  = first + ss->planetCount[s];
        for (int i = first; i < last; ++i) {
            DrawEllipseLines((int)ss->ringCx[i], (int)ss->ringCy[i],
                             ss->ringRx[i], ss->ringRy[i],
                             (Color){ 130, 150, 200, a });
        }
    }
}

/* Dibuja por rebanada: el alpha depende solo de la edad de la muestra, no del
 * cuerpo, asi que se calcula una vez por rebanada en vez de una vez por
 * segmento (TRAIL_LEN veces en vez de TRAIL_LEN*bodyCount). El recorrido de
 * memoria es lineal en x[][]/y[][], mismo eje que la escritura.
 *
 * ponytail: sin techo de segmentos por frame (bodyCount*TRAIL_LEN, ~196k en
 * el peor caso de N=256 sistemas llenos). Con N tipico (<=20) sobra margen;
 * si un N muy alto lo nota, saltar a dibujar 1 de cada 2 rebanadas.
 *
 * Sin guard de costura: los sistemas orbitan un anclaje fijo (sys_drift), ya
 * no hay teletransporte en los bordes, asi que cada segmento consecutivo es
 * siempre un paso corto real. */
static void render_trails(const TrailBuffer *tb)
{
    if (tb->fill < 2) {
        return;
    }

    const int oldest = (tb->head - tb->fill + TRAIL_LEN) % TRAIL_LEN;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < tb->fill - 1; ++k) {
        const int i0 = (oldest + k) % TRAIL_LEN;
        const int i1 = (oldest + k + 1) % TRAIL_LEN;

        const float t     = (float)(k + 1) / (float)tb->fill;
        const unsigned char a = alpha8(t * t * 0.55f);

        for (int b = 0; b < tb->bodyCount; ++b) {
            const Vector2 p0 = { tb->x[i0][b], tb->y[i0][b] };
            const Vector2 p1 = { tb->x[i1][b], tb->y[i1][b] };
            DrawLineEx(p0, p1, 2.2f, (Color){ tb->cr[b], tb->cg[b], tb->cb[b], a });
        }
    }
    EndBlendMode();
}

/* Resplandor aditivo de cada sol, atenuado por la capa de profundidad de su
 * sistema (los tres literales de alpha; el nucleo opaco se atenua aparte,
 * en render_bodies mas abajo). Global/sin ordenar por capa: aditivo
 * conmuta, no hace falta el orden atras->adelante que si necesita el pase
 * opaco de abajo. */
static void render_sun_glow(const World *w, const SolarSystems *ss)
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (int s = 0; s < ss->count; ++s) {
        const Entity e = ss->sun[s];
        if (e == ECS_INVALID) {
            continue; /* el escaneo global por mascara lo filtraba solo; el
                       * recorrido por sistema necesita este guard explicito */
        }
        const float   mul   = solar_layer_alpha(ss->layer[s]);
        const Vector2 p     = { w->px[e], w->py[e] };
        const float   r     = w->rad[e];
        const float   pulse = w->alpha[e];
        const uint8_t cr = w->cr[e], cg = w->cg[e], cb = w->cb[e];

        DrawCircleV(p, r * 5.0f * pulse, (Color){ cr, cg, cb, alpha8(0.07f * mul) });
        DrawCircleV(p, r * 2.8f * pulse, (Color){ cr, cg, cb, alpha8(0.14f * mul) });
        DrawCircleV(p, r * 1.5f * pulse, (Color){ cr, cg, cb, alpha8(0.35f * mul) });
    }
    EndBlendMode();
}

/* El pase que de verdad resuelve "cual esta adelante": por capa, atras
 * primero (SYS_LAYER_COUNT es chico — hoy 4 — asi que son baldes filtrados,
 * no un sort generico; dos sistemas en la misma capa comparten
 * escala/alpha, no hay un orden real que definir entre ellos). Dentro de
 * cada capa dibuja el sol de cada sistema y sus planetas, ambos atenuados
 * por capa (el nucleo del sol tambien — a diferencia de la primera version
 * de esta funcion, que lo dejaba a 255 fijo a proposito para no tocar un
 * comportamiento no pedido: el usuario pidio despues que la diferencia
 * entre capas se notara mas, y el sol es el elemento mas grande/brillante
 * de cada sistema, asi que dejarlo sin atenuar tapaba el efecto). Asi un
 * sistema de una capa mas al frente siempre tapa a uno de atras, sea
 * sol-sobre-sol, sol-sobre-planeta o planeta-sobre-planeta — antes, con
 * "todos los soles y despues todos los planetas" en orden de entidad, todo
 * planeta de la escena tapaba a todo sol sin importar el sistema.
 *
 * ponytail: sin orden intra-sistema (un planeta siempre se dibuja encima de
 * su propio sol, nunca detras, aunque su angulo de orbita lo pondria
 * "detras" en una vista con profundidad real). Barato de agregar con
 * sinf(oang[e]) si algun dia se nota; no es lo que se pidio. */
static void render_bodies(const World *w, const SolarSystems *ss)
{
    for (int L = 0; L < SYS_LAYER_COUNT; ++L) {
        const unsigned char a = alpha8(solar_layer_alpha(L));
        for (int s = 0; s < ss->count; ++s) {
            if (ss->layer[s] != L) {
                continue;
            }
            if (ss->sun[s] != ECS_INVALID) {
                const Entity e = ss->sun[s];
                DrawCircleV((Vector2){ w->px[e], w->py[e] }, w->rad[e],
                           (Color){ w->cr[e], w->cg[e], w->cb[e], a });
            }
            const int first = ss->ringFirst[s];
            const int last  = first + ss->planetCount[s];
            for (int i = first; i < last; ++i) {
                const Entity e = ss->ringEntity[i];
                DrawCircleV((Vector2){ w->px[e], w->py[e] }, w->rad[e],
                           (Color){ w->cr[e], w->cg[e], w->cb[e], a });
            }
        }
    }
}

/* Reflejo especular de cada planeta, atenuado por capa igual que su cuerpo
 * (render_bodies). Global/sin ordenar por capa por el mismo motivo que
 * render_sun_glow: aditivo, conmuta. */
static void render_specular(const World *w, const SolarSystems *ss)
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (int s = 0; s < ss->count; ++s) {
        const float mul = solar_layer_alpha(ss->layer[s]);
        const int first = ss->ringFirst[s];
        const int last  = first + ss->planetCount[s];
        for (int i = first; i < last; ++i) {
            const Entity e = ss->ringEntity[i];
            const float  r = w->rad[e];
            if (r <= 2.0f) {
                continue;
            }
            DrawCircleV((Vector2){ w->px[e] - r * 0.3f, w->py[e] - r * 0.3f }, r * 0.45f,
                       (Color){ 255, 255, 255, alpha8(60.0f / 255.0f * mul) });
        }
    }
    EndBlendMode();
}

void sys_render(const World *w, const SolarSystems *ss, const TrailBuffer *tb,
                int showRings, int showTrails)
{
    render_starfield(w);
    if (showRings) {
        render_rings(ss);
    }
    if (showTrails) {
        render_trails(tb);
    }
    render_sun_glow(w, ss);
    render_bodies(w, ss);
    render_specular(w, ss);
}

void sys_render_parallel(const World *w, const SolarSystems *ss, const TrailBuffer *tb,
                         int showRings, int showTrails)
{
    sys_render(w, ss, tb, showRings, showTrails);
}
