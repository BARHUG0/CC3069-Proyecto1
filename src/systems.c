#include "systems.h"

#include <assert.h>
#include <math.h>

#include "raylib.h"

#define FADE_IN_SECS  0.8f
#define FADE_OUT_SECS 1.2f

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

void sys_twinkle(World *w, float t)
{
    const uint32_t n    = w->highWater;
    const uint32_t want = C_RENDER | C_TWINKLE;

    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & want) != want) {
            continue;
        }
        w->alpha[e] = clamp01(w->twBase[e] +
                              w->twAmp[e] * sinf(w->twFreq[e] * t + w->twPhase[e]));
    }
}

void sys_drift(World *w, SolarSystems *ss, float screenW, float screenH, float dt)
{
    for (int s = 0; s < ss->count; ++s) {
        float cx = ss->cx[s] + ss->vx[s] * dt;
        float cy = ss->cy[s] + ss->vy[s] * dt;

        const float r = ss->ext[s];

        /* Envuelve cuando el sistema entero (centro +/- r) cruza el borde:
         * reaparece justo del otro lado en el mismo instante en que el ultimo
         * pixel abandona la pantalla, sin hueco ni duplicado. */
        if (cx >  screenW + r) cx -= (screenW + 2.0f * r);
        if (cx < -r)           cx += (screenW + 2.0f * r);
        if (cy >  screenH + r) cy -= (screenH + 2.0f * r);
        if (cy < -r)           cy += (screenH + 2.0f * r);

        assert(cx >= -r - 0.5f && cx <= screenW + r + 0.5f);
        assert(cy >= -r - 0.5f && cy <= screenH + r + 0.5f);

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
}

void sys_orbit(World *w, float dt)
{
    const uint32_t n    = w->highWater;
    const uint32_t want = C_ORBIT | C_POS;

    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & want) != want) {
            continue;
        }
        float a = w->oang[e] + w->ospd[e] * dt;

        /* Envolver el angulo evita que crezca sin limite: con float, un angulo
         * grande pierde precision y las orbitas empiezan a saltar. */
        if (a > 6.2831853f)  a -= 6.2831853f;
        if (a < 0.0f)        a += 6.2831853f;

        w->oang[e] = a;
        w->px[e]   = w->ocx[e] + w->orx[e] * cosf(a);
        w->py[e]   = w->ocy[e] + w->ory[e] * sinf(a);
    }
}

int sys_lifetime(World *w, float dt)
{
    const uint32_t n = w->highWater;
    int killed = 0;

    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & C_LIFE) == 0u) {
            continue;
        }

        const float life = w->life[e] - dt;
        if (life <= 0.0f) {
            ecs_destroy(w, e);
            killed++;
            continue;
        }
        w->life[e] = life;

        /* Sobre trapezoidal: entra en FADE_IN_SECS, sale en FADE_OUT_SECS. */
        const float age = w->lifeMax[e] - life;
        float env = 1.0f;
        if (age < FADE_IN_SECS)   env  = age / FADE_IN_SECS;
        if (life < FADE_OUT_SECS) env *= life / FADE_OUT_SECS;

        w->alpha[e] *= env;
    }
    return killed;
}

/* --- estelas -------------------------------------------------------------- */

void trails_init(TrailBuffer *tb, const World *w, const SolarSystems *ss,
                 float screenW, float screenH)
{
    tb->screenW = screenW;
    tb->screenH = screenH;

    int n = 0;
    for (int s = 0; s < ss->count && n < MAX_TRAIL_BODIES; ++s) {
        if (ss->sun[s] == ECS_INVALID) {
            continue;
        }
        tb->body[n] = ss->sun[s];
        tb->cr[n]   = w->cr[ss->sun[s]];
        tb->cg[n]   = w->cg[ss->sun[s]];
        tb->cb[n]   = w->cb[ss->sun[s]];
        n++;
    }
    for (int i = 0; i < ss->ringTotal && n < MAX_TRAIL_BODIES; ++i) {
        const Entity pe = ss->ringEntity[i];
        tb->body[n] = pe;
        tb->cr[n]   = w->cr[pe];
        tb->cg[n]   = w->cg[pe];
        tb->cb[n]   = w->cb[pe];
        n++;
    }

    tb->bodyCount = n;
    tb->head      = 0;
    tb->fill      = 0;
    tb->accum     = 0.0f;
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

static void render_rings(const SolarSystems *ss)
{
    for (int i = 0; i < ss->ringTotal; ++i) {
        DrawEllipseLines((int)ss->ringCx[i], (int)ss->ringCy[i],
                         ss->ringRx[i], ss->ringRy[i],
                         (Color){ 130, 150, 200, 30 });
    }
}

/* Dibuja por rebanada: el alpha depende solo de la edad de la muestra, no del
 * cuerpo, asi que se calcula una vez por rebanada en vez de una vez por
 * segmento (TRAIL_LEN veces en vez de TRAIL_LEN*bodyCount). El recorrido de
 * memoria es lineal en x[][]/y[][], mismo eje que la escritura.
 *
 * ponytail: sin techo de segmentos por frame (bodyCount*TRAIL_LEN, ~196k en
 * el peor caso de N=256 sistemas llenos). Con N tipico (<=20) sobra margen;
 * si un N muy alto lo nota, saltar a dibujar 1 de cada 2 rebanadas. */
static void render_trails(const TrailBuffer *tb)
{
    if (tb->fill < 2) {
        return;
    }

    const int oldest = (tb->head - tb->fill + TRAIL_LEN) % TRAIL_LEN;
    const float halfW = tb->screenW * 0.5f;
    const float halfH = tb->screenH * 0.5f;

    BeginBlendMode(BLEND_ADDITIVE);
    for (int k = 0; k < tb->fill - 1; ++k) {
        const int i0 = (oldest + k) % TRAIL_LEN;
        const int i1 = (oldest + k + 1) % TRAIL_LEN;

        const float t     = (float)(k + 1) / (float)tb->fill;
        const unsigned char a = alpha8(t * t * 0.55f);

        for (int b = 0; b < tb->bodyCount; ++b) {
            const Vector2 p0 = { tb->x[i0][b], tb->y[i0][b] };
            const Vector2 p1 = { tb->x[i1][b], tb->y[i1][b] };

            /* Guard de costura: un salto por wrap mueve al cuerpo casi el
             * ancho/alto de pantalla en un solo tick; un paso real a 24 Hz
             * son unos pocos pixeles. Sin esto, envolver dibujaria un rayajo
             * cruzando toda la pantalla. */
            if (fabsf(p1.x - p0.x) > halfW || fabsf(p1.y - p0.y) > halfH) {
                continue;
            }

            DrawLineEx(p0, p1, 1.6f, (Color){ tb->cr[b], tb->cg[b], tb->cb[b], a });
        }
    }
    EndBlendMode();
}

static void render_suns(const World *w)
{
    const uint32_t n = w->highWater;

    BeginBlendMode(BLEND_ADDITIVE);
    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & C_SUN) == 0u) {
            continue;
        }
        const Vector2 p     = { w->px[e], w->py[e] };
        const float   r     = w->rad[e];
        const float   pulse = w->alpha[e];
        const uint8_t cr = w->cr[e], cg = w->cg[e], cb = w->cb[e];

        DrawCircleV(p, r * 5.0f * pulse, (Color){ cr, cg, cb, alpha8(0.07f) });
        DrawCircleV(p, r * 2.8f * pulse, (Color){ cr, cg, cb, alpha8(0.14f) });
        DrawCircleV(p, r * 1.5f * pulse, (Color){ cr, cg, cb, alpha8(0.35f) });
    }
    EndBlendMode();

    /* Nucleo opaco aparte para que no se sature a blanco puro. */
    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & C_SUN) == 0u) {
            continue;
        }
        DrawCircleV((Vector2){ w->px[e], w->py[e] }, w->rad[e],
                    (Color){ w->cr[e], w->cg[e], w->cb[e], 255 });
    }
}

static void render_planets(const World *w)
{
    const uint32_t n    = w->highWater;
    const uint32_t want = C_ORBIT | C_RENDER;

    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & want) != want) {
            continue;
        }
        DrawCircleV((Vector2){ w->px[e], w->py[e] }, w->rad[e],
                    (Color){ w->cr[e], w->cg[e], w->cb[e], 255 });
    }

    /* Los reflejos van en una sola pasada aditiva. Alternar el modo de mezcla
     * por planeta obligaria a vaciar el lote de rlgl en cada iteracion, o sea
     * cientos de draw calls por fotograma en vez de unas pocas. */
    BeginBlendMode(BLEND_ADDITIVE);
    for (uint32_t e = 0; e < n; ++e) {
        if ((w->mask[e] & want) != want || w->rad[e] <= 2.0f) {
            continue;
        }
        const float r = w->rad[e];
        DrawCircleV((Vector2){ w->px[e] - r * 0.3f, w->py[e] - r * 0.3f }, r * 0.45f,
                    (Color){ 255, 255, 255, 60 });
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
    render_suns(w);
    render_planets(w);
}
