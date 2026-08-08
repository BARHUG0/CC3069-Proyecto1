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

static void render_starfield(const World *w)
{
    const uint32_t n = w->highWater;
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

void sys_render(const World *w, const SolarSystems *ss, int showRings)
{
    (void)ss;
    (void)showRings;
    render_starfield(w);
}
