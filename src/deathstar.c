#include "deathstar.h"

#include <math.h>

#include "raymath.h"

#define DS_TWO_PI      6.2831853f
#define DS_CHARGE_SECS 0.9f
#define DS_FIRE_SECS   0.35f
#define DS_EXP_DUR     1.6f
#define DS_SHOCK_MAXR  140.0f

/* El ojo vive siempre a un lado del cuerpo, nunca en el centro (ver
 * deathstar.h): azimut fijo desde el polo frontal, mas una leve inclinacion
 * hacia el hemisferio norte, como en las referencias de la nave. */
#define DS_DISH_AZ_DEG 42.0f
#define DS_DISH_EL_DEG 15.0f

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

/* --- texturas procedurales -------------------------------------------------
 * Sin archivos de asset: todo con GenImageColor + Image*. */

/* Cuerpo: GenMeshSphere, u = longitud [0,1) en X de textura, v = latitud
 * [0,1] en Y de textura (0 = polo norte, 1 = polo sur) — convencion estandar,
 * confirmada dibujando UNA banda a la vez y mirando el resultado real sobre
 * la esfera (rondas anteriores de esta sesion habian invertido esto dos
 * veces por leer mal una prueba ambigua; esta vez la prueba fue una banda
 * rellena, sin ambiguedad: X constante -> arco vertical polo a polo
 * (meridiano); Y constante -> anillo horizontal a latitud fija (paralelo)).
 * Por eso:
 *   - paneles verticales (meridianos) -> lineas de X constante (bucle "for x").
 *   - paneles horizontales (paralelos) -> lineas de Y constante (bucle "for y").
 *   - trinchera ecuatorial (anillo a latitud fija = ecuador) -> banda
 *     HORIZONTAL en la imagen (Y constante, ancho completo en X). Vista de
 *     frente se ve como un anillo limpio cruzando el ecuador; vista casi de
 *     canto (en ciertos angulos de rotacion) se ve como una cuna angosta por
 *     el escorzo normal de una banda plana en perspectiva — inevitable con
 *     cualquier motor 3D sin trucos extra, no es un bug de eje. */
static Texture2D ds_build_skin(Rng *rng)
{
    const int texW = 512, texH = 256;
    Image img = GenImageColor(texW, texH, (Color){ 150, 150, 156, 255 });

    /* Meridianos (verticales) + paralelos (horizontales) — rejilla completa. */
    for (int x = 0; x < texW; x += 32) {
        ImageDrawLine(&img, x, 0, x, texH - 1, (Color){ 92, 92, 100, 255 });
    }
    for (int y = 0; y < texH; y += 16) {
        ImageDrawLine(&img, 0, y, texW - 1, y, (Color){ 92, 92, 100, 255 });
    }

    const int eqY  = texH / 2;
    const int half = 8;
    ImageDrawRectangle(&img, 0, eqY - half, texW, half * 2, (Color){ 68, 68, 76, 255 });
    ImageDrawLine(&img, 0, eqY, texW - 1, eqY, (Color){ 36, 36, 42, 255 });

    for (int i = 0; i < 140; ++i) {
        const int lx = (int)rng_range(rng, 0.0f, (float)texW);
        const int ly = (int)rng_range(rng, 4.0f, (float)(texH - 4));
        ImageDrawCircle(&img, lx, ly, 1, (Color){ 255, 214, 110, 255 });
    }

    const Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

/* Ojo: degradado radial concentrico horneado en una textura real (128x128),
 * aplicado a la cara del plato — reemplaza el truco de v2 (circulos 2D
 * superpuestos) por una textura de verdad, ahora que hay GPU otra vez. */
static Texture2D ds_build_dish_skin(void)
{
    const int size = 128;
    const int cx = size / 2, cy = size / 2;
    Image img = GenImageColor(size, size, (Color){ 40, 44, 40, 255 });

    ImageDrawCircle(&img, cx, cy, size / 2,             (Color){ 62, 65, 60, 255 });
    ImageDrawCircle(&img, cx, cy, (int)(size * 0.40f),  (Color){ 84, 88, 80, 255 });
    ImageDrawCircle(&img, cx, cy, (int)(size * 0.30f),  (Color){ 52, 58, 52, 255 });
    ImageDrawCircle(&img, cx, cy, (int)(size * 0.18f),  (Color){ 30, 36, 32, 255 });
    ImageDrawCircle(&img, cx, cy, (int)(size * 0.08f),  (Color){ 140, 255, 175, 255 });
    ImageDrawCircle(&img, cx - size / 5, cy - size / 5, size / 11, (Color){ 225, 235, 225, 255 });
    ImageDrawCircleLines(&img, cx, cy, size / 2 - 1, (Color){ 20, 22, 20, 255 });

    const Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

/* Reposiciona el ojo segun ds->dishSide. Independiente de ds->spin a
 * proposito: si siguiera la rotacion del cuerpo, en cada vuelta pasaria por
 * el centro. Solo se llama cuando dishSide cambia (al elegir cada disparo
 * nuevo), no cada frame. */
static void ds_place_dish(DeathStar *ds)
{
    const float az = DS_DISH_AZ_DEG * DEG2RAD;
    const float el = DS_DISH_EL_DEG * DEG2RAD;

    Vector3 n = {
        (float)ds->dishSide * sinf(az) * cosf(el),
        sinf(el),
        cosf(az) * cosf(el)
    };
    n = Vector3Normalize(n);
    ds->dishPos = Vector3Scale(n, ds->worldR);

    const Vector3 up = { 0.0f, 1.0f, 0.0f };
    const float   d  = Vector3DotProduct(up, n);
    if (d > 0.9999f) {
        ds->dishRotAxis  = (Vector3){ 1.0f, 0.0f, 0.0f };
        ds->dishRotAngle = 0.0f;
    } else if (d < -0.9999f) {
        ds->dishRotAxis  = (Vector3){ 1.0f, 0.0f, 0.0f };
        ds->dishRotAngle = 180.0f;
    } else {
        ds->dishRotAxis  = Vector3Normalize(Vector3CrossProduct(up, n));
        ds->dishRotAngle = acosf(ds_clampf(d, -1.0f, 1.0f)) * RAD2DEG;
    }
}

void deathstar_load(DeathStar *ds, Rng *rng, float secs)
{
    ds->cam.position   = (Vector3){ 0.0f, 0.0f, 6.0f };
    ds->cam.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    ds->cam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    ds->cam.fovy       = 45.0f;
    ds->cam.projection = CAMERA_PERSPECTIVE;

    /* worldR no depende de la resolucion: GetWorldToScreen/el proyector de
     * raylib cubren el alto completo de la ventana con fovy en Y sin
     * importar el ancho, asi que este radio ocupa siempre la misma fraccion
     * de pantalla y el resize no toca nada. */
    const float frac = 0.30f;
    ds->worldR = frac * ds->cam.position.z * tanf(ds->cam.fovy * 0.5f * DEG2RAD);
    ds->dishR  = ds->worldR * 0.22f;
    const float dishH = ds->dishR * 0.16f;

    ds->skin = ds_build_skin(rng);
    ds->body = LoadModelFromMesh(GenMeshSphere(ds->worldR, 24, 32));
    SetMaterialTexture(&ds->body.materials[0], MATERIAL_MAP_DIFFUSE, ds->skin);

    ds->dishSkin = ds_build_dish_skin();
    ds->dish     = LoadModelFromMesh(GenMeshCylinder(ds->dishR, dishH, 24));
    SetMaterialTexture(&ds->dish.materials[0], MATERIAL_MAP_DIFFUSE, ds->dishSkin);

    ds->spin     = 0.0f;
    ds->dishSide = (rng_below(rng, 2u) == 0u) ? 1 : -1;
    ds_place_dish(ds);

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

void deathstar_unload(DeathStar *ds)
{
    UnloadModel(ds->body);
    UnloadModel(ds->dish);
    UnloadTexture(ds->skin);
    UnloadTexture(ds->dishSkin);
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
    ds->spin += 6.0f * dt; /* grados/seg, una vuelta cada 60s */
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
            /* El ojo alterna de lado en cada disparo (decision explicita del
             * usuario) — se decide ANTES de elegir el objetivo, para que el
             * objetivo quede acotado a esa mitad: el ojo nunca dispara al
             * lado contrario de donde esta. */
            ds->dishSide = -ds->dishSide;
            ds_place_dish(ds);

            const float halfW = screenW * 0.5f;
            ds->aimX = (ds->dishSide < 0) ? rng_range(rng, 0.0f, halfW)
                                          : rng_range(rng, halfW, screenW);
            ds->aimY = rng_range(rng, 0.0f, screenH);
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

/* --- render ----------------------------------------------------------- */

/* Los 8 haces del borde del ojo convergen en un foco cerca de la estacion, y
 * de ahi sale un solo rayo grueso hasta el punto de impacto real. El ojo no
 * se mueve segun el objetivo (vive a un lado fijo por disparo), asi que el
 * rayo simplemente sale de donde el ojo este hacia el pixel real: no hay
 * forma de que quede desalineado. */
static void ds_render_beam(const DeathStar *ds)
{
    if (ds->phase != DS_CHARGE && ds->phase != DS_FIRE) {
        return;
    }

    const Vector3 up  = { 0.0f, 1.0f, 0.0f };
    Vector3 nrm = Vector3Normalize(ds->dishPos);
    Vector3 tangent = (fabsf(Vector3DotProduct(nrm, up)) > 0.98f)
        ? Vector3CrossProduct(nrm, (Vector3){ 1.0f, 0.0f, 0.0f })
        : Vector3CrossProduct(nrm, up);
    tangent = Vector3Normalize(tangent);
    const Vector3 bitangent = Vector3CrossProduct(nrm, tangent);

    const Vector2 dishScreen = GetWorldToScreen(ds->dishPos, ds->cam);

    const float chargeT = (ds->phase == DS_FIRE) ? 1.0f
        : ds_clampf(1.0f - ds->timer / DS_CHARGE_SECS, 0.0f, 1.0f);

    Vector2 rim[8];
    for (int i = 0; i < 8; ++i) {
        const float theta = (float)i * (DS_TWO_PI / 8.0f);
        const Vector3 p = Vector3Add(ds->dishPos,
            Vector3Add(Vector3Scale(tangent, ds->dishR * cosf(theta)),
                      Vector3Scale(bitangent, ds->dishR * sinf(theta))));
        rim[i] = GetWorldToScreen(p, ds->cam);
    }

    const Vector2 focus = {
        dishScreen.x + (ds->aimX - dishScreen.x) * 0.18f,
        dishScreen.y + (ds->aimY - dishScreen.y) * 0.18f
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

        DrawCircleLines((int)c.x, (int)c.y, t * DS_SHOCK_MAXR,
                        (Color){ 255, 200, 120, ds_alpha8((1.0f - t) * 0.7f) });
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
    (void)screenW;
    (void)screenH;

    BeginMode3D(ds->cam);

    DrawModelEx(ds->body, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f },
               ds->spin, (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);

    /* DrawModelEx, no DrawMesh(mesh, material, MatrixMultiply(rot, trans)):
     * la version con Matrix manual corrompia el render (la esfera salia
     * gigante y recortada en una esquina, reproducible en frames concretos)
     * — aislado con una prueba binaria (quitar el dibujo del ojo arreglaba
     * el cuerpo), la causa no era la matriz en si (los valores eran finitos
     * y sanos) sino la llamada a DrawMesh cruda. DrawModelEx logra la misma
     * rotacion+traslacion y ya esta probado con el cuerpo. */
    DrawModelEx(ds->dish, ds->dishPos, ds->dishRotAxis, ds->dishRotAngle,
               (Vector3){ 1.0f, 1.0f, 1.0f }, WHITE);

    EndMode3D();

    ds_render_beam(ds);
    ds_render_explosions(ds);
}
