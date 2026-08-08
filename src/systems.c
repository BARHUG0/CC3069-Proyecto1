#include "systems.h"
#include <math.h>
#include "raylib.h"

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

static void render_starfield(const World *w)
{
    const uint32_t n = w->highWater;
    const uint32_t want = C_RENDER; /* No exigimos TWINKLE todavía para pintar las estáticas */

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
    /* Ignoramos los parámetros no usados por ahora */
    (void)ss;
    (void)showRings;
    render_starfield(w);
}
