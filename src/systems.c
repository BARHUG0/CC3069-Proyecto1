#include "systems.h"

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

void sys_render(const World *w, const SolarSystems *ss, int showRings)
{
    render_starfield(w);
    if (showRings) {
        render_rings(ss);
    }
    render_suns(w);
    render_planets(w);
}
