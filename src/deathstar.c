#include "deathstar.h"

#include <math.h>

#define DS_TWO_PI      6.2831853f
#define DS_DEG2RAD     0.0174532925f
#define DS_CHARGE_SECS 0.9f
#define DS_FIRE_SECS   0.35f
#define DS_EXP_DUR     1.6f
#define DS_SHOCK_MAXR  140.0f

static float ds_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static unsigned char ds_alpha8(float a)
{
    return (unsigned char)(ds_clampf(a, 0.0f, 1.0f) * 255.0f);
}

void deathstar_load(DeathStar *ds, Rng *rng, float secs)
{
    ds->spin     = 0.0f;
    ds->bodyFrac = 0.34f;
    ds->dishFrac = 0.22f; /* chico, y siempre en el centro: ver deathstar.h */

    /* Luces amarillas por todo el disco: muestreo uniforme en area (raiz
     * cuadrada del radio, no el radio mismo, o se amontonarian en el centro)
     * entre el borde del ojo y el borde del cuerpo. Offsets normalizados
     * (se escalan por el radio real del cuerpo al dibujar) y fijos desde
     * aqui, para que no salten de frame a frame. */
    ds->lightCount = DS_MAX_LIGHTS;
    for (int i = 0; i < DS_MAX_LIGHTS; ++i) {
        const float ang = rng_range(rng, 0.0f, DS_TWO_PI);
        const float rad = ds->dishFrac * 1.4f +
            sqrtf(rng_f01(rng)) * (0.94f - ds->dishFrac * 1.4f);
        ds->lightDX[i] = cosf(ang) * rad;
        ds->lightDY[i] = sinf(ang) * rad;
        ds->lightR[i]  = rng_range(rng, 0.010f, 0.020f);
    }

    ds->aimX  = 0.0f;
    ds->aimY  = 0.0f;
    ds->blastR = 90.0f; /* deathstar_update lo reescala con ss->cellR en cuanto corre */

    /* interval ANTES de reset: deathstar_reset siembra timer/respawnTimer a
     * partir de ds->interval, asi que si se llama con el valor de sobra en
     * el stack, IDLE arranca con una cuenta atras basura (tipicamente
     * enorme) y el primer disparo nunca llega. */
    ds->interval = (secs > 0.0f) ? secs : 5.0f;
    deathstar_reset(ds);
}

void deathstar_reset(DeathStar *ds)
{
    ds->phase        = DS_IDLE;
    ds->timer        = ds->interval > 0.0f ? ds->interval : 5.0f;
    ds->respawnTimer = ds->timer;
    ds->kills        = 0;
    ds->expCount     = 0;
}

/* --- explosiones ----------------------------------------------------------
 * Tabla aplanada: la explosion i ocupa
 * [i*DS_EXP_PARTICLES, (i+1)*DS_EXP_PARTICLES) en partDir/partSpd. */
static void ds_spawn_explosion(DeathStar *ds, Rng *rng, float cx, float cy)
{
    if (ds->expCount >= DS_MAX_EXPLOSIONS) {
        return; /* explosiones simultaneas de sobra para el ritmo de disparo */
    }
    const int i = ds->expCount++;
    ds->expX[i]   = cx;
    ds->expY[i]   = cy;
    ds->expAge[i] = 0.0f;
    ds->expDur[i] = DS_EXP_DUR;

    for (int k = 0; k < DS_EXP_PARTICLES; ++k) {
        const int idx = i * DS_EXP_PARTICLES + k;
        ds->partDir[idx] = rng_range(rng, 0.0f, DS_TWO_PI);
        ds->partSpd[idx] = rng_range(rng, 60.0f, 220.0f);
    }
}

static void ds_update_explosions(DeathStar *ds, float dt)
{
    for (int i = 0; i < ds->expCount; ) {
        ds->expAge[i] += dt;
        if (ds->expAge[i] < ds->expDur[i]) {
            i++;
            continue;
        }
        /* Swap-remove: acorta expCount, copia la ultima explosion (y su
         * bloque de particulas) encima de la que acaba de morir. */
        const int last = ds->expCount - 1;
        if (i != last) {
            ds->expX[i]   = ds->expX[last];
            ds->expY[i]   = ds->expY[last];
            ds->expAge[i] = ds->expAge[last];
            ds->expDur[i] = ds->expDur[last];
            for (int k = 0; k < DS_EXP_PARTICLES; ++k) {
                ds->partDir[i * DS_EXP_PARTICLES + k] = ds->partDir[last * DS_EXP_PARTICLES + k];
                ds->partSpd[i * DS_EXP_PARTICLES + k] = ds->partSpd[last * DS_EXP_PARTICLES + k];
            }
        }
        ds->expCount--;
        /* no i++: reprocesar el slot i, que ahora tiene la explosion movida */
    }
}

void deathstar_update(DeathStar *ds, World *w, SolarSystems *ss, TrailBuffer *tb,
                      Rng *rng, int targetN, float screenW, float screenH, float dt)
{
    ds->spin += 4.0f * dt; /* grados/seg, una vuelta cada 90s */
    if (ds->spin >= 360.0f) {
        ds->spin -= 360.0f;
    }

    /* El radio de impacto se ata a la escala real de los sistemas (cellR ya
     * refleja el tamano de celda para el N actual), no a un pixel fijo que se
     * sentiria enorme con N chico o inutil con N grande. */
    if (ss->cellR > 1.0f) {
        ds->blastR = ss->cellR * 1.3f;
    }

    switch (ds->phase) {
    case DS_IDLE:
        ds->timer -= dt;
        if (ds->timer <= 0.0f) {
            ds->aimX  = rng_range(rng, 0.0f, screenW);
            ds->aimY  = rng_range(rng, 0.0f, screenH);
            ds->phase = DS_CHARGE;
            ds->timer = DS_CHARGE_SECS;
        }
        break;

    case DS_CHARGE:
        ds->timer -= dt;
        if (ds->timer <= 0.0f) {
            ds->phase = DS_FIRE;
            ds->timer = DS_FIRE_SECS;

            /* Resolver impacto: el primer sistema dentro del radio de
             * explosion (normalmente 0, a veces 1; el disparo apunta a un
             * punto al azar, no a un sistema, asi que puede fallar). */
            int victim = -1;
            for (int s = 0; s < ss->count; ++s) {
                const float ddx = ss->cx[s] - ds->aimX;
                const float ddy = ss->cy[s] - ds->aimY;
                if (ddx * ddx + ddy * ddy < ds->blastR * ds->blastR) {
                    victim = s;
                    break;
                }
            }
            if (victim >= 0) {
                ds_spawn_explosion(ds, rng, ss->cx[victim], ss->cy[victim]);
                trails_drop_system(tb, ss, victim);
                solar_system_remove(w, ss, victim);
                ds->kills++;
            }
        }
        break;

    case DS_FIRE:
        ds->timer -= dt;
        if (ds->timer <= 0.0f) {
            ds->phase = DS_IDLE;
            ds->timer = ds->interval;
        }
        break;
    }

    ds_update_explosions(ds, dt);

    /* Respawn incremental: la poblacion nunca llega a cero, la estacion
     * destruye y la galaxia repone de a uno. */
    if (ss->count < targetN) {
        ds->respawnTimer -= dt;
        if (ds->respawnTimer <= 0.0f) {
            const int s = spawn_one_system(w, ss, rng);
            if (s >= 0) {
                trails_add_system(tb, w, ss, s);
            }
            ds->respawnTimer = ds->interval;
        }
    } else {
        ds->respawnTimer = ds->interval;
    }
}

/* --- render -----------------------------------------------------------
 * Todo en 2D, con las mismas primitivas que sys_render (DrawCircleV,
 * DrawLineEx...): nada de Camera3D/Model ni sombreado falso — el cuerpo es
 * un disco plano con paneles, igual de "flat" que soles y planetas. */

static void ds_render_body(const DeathStar *ds, float cx, float cy, float bodyR)
{
    DrawCircleV((Vector2){ cx, cy }, bodyR, (Color){ 150, 150, 156, 255 });

    /* Paneles: anillos concentricos + radios, como un esquema tecnico. */
    for (int i = 1; i <= 4; ++i) {
        DrawCircleLines((int)cx, (int)cy, bodyR * (float)i / 5.0f, (Color){ 110, 110, 118, 150 });
    }
    const float dishR = bodyR * ds->dishFrac;
    const int   spokes = 12;
    for (int i = 0; i < spokes; ++i) {
        const float a  = ds->spin * DS_DEG2RAD + (float)i * (DS_TWO_PI / (float)spokes);
        const float ca = cosf(a), sa = sinf(a);
        const Vector2 p0 = { cx + ca * dishR * 1.15f, cy + sa * dishR * 1.15f };
        const Vector2 p1 = { cx + ca * bodyR,         cy + sa * bodyR };
        DrawLineEx(p0, p1, 1.0f, (Color){ 110, 110, 118, 130 });
    }

    /* Trinchera ecuatorial: el ancho se calcula con el borde del circulo en
     * el extremo mas angosto de la banda, asi nunca se sale del disco. */
    const float trenchHalf = bodyR * 0.07f;
    const float halfW = sqrtf(fmaxf(bodyR * bodyR - trenchHalf * trenchHalf, 0.0f));
    DrawRectangle((int)(cx - halfW), (int)(cy - trenchHalf), (int)(halfW * 2.0f),
                 (int)(trenchHalf * 2.0f), (Color){ 76, 76, 84, 255 });
    DrawLineEx((Vector2){ cx - halfW, cy }, (Vector2){ cx + halfW, cy }, 1.0f,
              (Color){ 40, 40, 46, 255 });

    DrawCircleLines((int)cx, (int)cy, bodyR, (Color){ 55, 55, 62, 220 });

    /* Luces amarillas por todo el disco, rotando con el spin. */
    const float sa = sinf(ds->spin * DS_DEG2RAD), ca = cosf(ds->spin * DS_DEG2RAD);
    for (int i = 0; i < ds->lightCount; ++i) {
        const float dx = ds->lightDX[i], dy = ds->lightDY[i];
        const Vector2 p = { cx + (dx * ca - dy * sa) * bodyR, cy + (dx * sa + dy * ca) * bodyR };
        DrawCircleV(p, ds->lightR[i] * bodyR, (Color){ 255, 214, 110, 235 });
    }
}

/* El ojo: chico y siempre en el centro geometrico del cuerpo (nunca se
 * desplaza ni rota para "apuntar" — el rayo mismo va del centro al objetivo).
 * Anillos concentricos que oscurecen hacia el centro + un brillo
 * especular desplazado dan sensacion de superficie concava, sin textura ni
 * shader: el mismo truco que ya usan los soles (halos concentricos, ver
 * render_suns en systems.c) aplicado con la gradacion invertida. */
static void ds_render_dish(float cx, float cy, float dishR)
{
    DrawCircleV((Vector2){ cx, cy }, dishR,          (Color){ 72, 74, 72, 255 });
    DrawCircleV((Vector2){ cx, cy }, dishR * 0.80f,  (Color){ 96, 100, 94, 255 });
    DrawCircleV((Vector2){ cx, cy }, dishR * 0.58f,  (Color){ 60, 65, 60, 255 });
    DrawCircleV((Vector2){ cx, cy }, dishR * 0.36f,  (Color){ 38, 44, 40, 255 });
    DrawCircleV((Vector2){ cx, cy }, dishR * 0.16f,  (Color){ 130, 255, 165, 255 });
    DrawCircleV((Vector2){ cx - dishR * 0.28f, cy - dishR * 0.30f }, dishR * 0.16f,
               (Color){ 255, 255, 255, 80 });
}

/* Los 8 haces del borde del ojo convergen en un foco cerca del centro, y de
 * ahi sale un solo rayo grueso hasta el punto de impacto real. Como el ojo
 * ya no rota para apuntar, el rayo va derecho del centro al objetivo: no hay
 * forma de que "dispare al frente y no le pegue a nada". */
static void ds_render_beam(const DeathStar *ds, float cx, float cy, float dishR)
{
    if (ds->phase != DS_CHARGE && ds->phase != DS_FIRE) {
        return;
    }

    const float chargeT = (ds->phase == DS_FIRE) ? 1.0f
        : ds_clampf(1.0f - ds->timer / DS_CHARGE_SECS, 0.0f, 1.0f);

    Vector2 rim[8];
    for (int i = 0; i < 8; ++i) {
        const float theta = (float)i * (DS_TWO_PI / 8.0f);
        rim[i] = (Vector2){ cx + cosf(theta) * dishR, cy + sinf(theta) * dishR };
    }

    const Vector2 focus = {
        cx + (ds->aimX - cx) * 0.22f,
        cy + (ds->aimY - cy) * 0.22f
    };

    const Color beamColor = { 90, 255, 130, ds_alpha8(0.85f * chargeT) };

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 8; ++i) {
        DrawLineEx(rim[i], focus, 2.0f, beamColor);
    }
    if (ds->phase == DS_FIRE) {
        const float fireT = 1.0f - ds->timer / DS_FIRE_SECS;
        const Color mainColor = { 150, 255, 170, ds_alpha8(1.0f - fireT * 0.3f) };
        DrawLineEx(focus, (Vector2){ ds->aimX, ds->aimY }, 6.0f, mainColor);
        DrawCircleV((Vector2){ ds->aimX, ds->aimY }, 10.0f + 6.0f * fireT,
                   (Color){ 200, 255, 210, ds_alpha8(0.6f * (1.0f - fireT)) });
    }
    EndBlendMode();
}

static void ds_render_explosions(const DeathStar *ds)
{
    if (ds->expCount == 0) {
        return;
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < ds->expCount; ++i) {
        const float t = ds_clampf(ds->expAge[i] / ds->expDur[i], 0.0f, 1.0f);
        const Vector2 c = { ds->expX[i], ds->expY[i] };

        /* Onda expansiva. */
        DrawCircleLines((int)c.x, (int)c.y, t * DS_SHOCK_MAXR,
                        (Color){ 255, 200, 120, ds_alpha8((1.0f - t) * 0.7f) });
        /* Destello que decae. */
        DrawCircleV(c, (1.0f - t) * 34.0f + 4.0f,
                   (Color){ 255, 230, 180, ds_alpha8((1.0f - t) * (1.0f - t)) });

        for (int k = 0; k < DS_EXP_PARTICLES; ++k) {
            const int   idx  = i * DS_EXP_PARTICLES + k;
            const float dist = ds->partSpd[idx] * ds->expAge[i];
            if (dist > DS_SHOCK_MAXR * 1.2f) {
                continue;
            }
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * dist,
                c.y + sinf(ds->partDir[idx]) * dist
            };
            DrawCircleV(p, (1.0f - t) * 2.2f + 0.6f,
                       (Color){ 255, 180, 90, ds_alpha8((1.0f - t) * 0.85f) });
        }
    }
    EndBlendMode();
}

void deathstar_render(const DeathStar *ds, int screenW, int screenH)
{
    const float cx    = (float)screenW * 0.5f;
    const float cy    = (float)screenH * 0.5f;
    const float bodyR = ds->bodyFrac * (float)screenH * 0.5f;
    const float dishR = bodyR * ds->dishFrac;

    ds_render_body(ds, cx, cy, bodyR);
    ds_render_dish(cx, cy, dishR);
    ds_render_beam(ds, cx, cy, dishR);
    ds_render_explosions(ds);
}
