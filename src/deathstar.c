#include "deathstar.h"

#include <math.h>

#include "raymath.h"

#define DS_TWO_PI      6.2831853f
#define DS_CHARGE_SECS 0.9f
#define DS_FIRE_SECS   0.35f
#define DS_EXP_DUR     1.6f
#define DS_SHOCK_MAXR  140.0f

/* El ojo esta fijo a un punto del cuerpo (arriba, con una leve inclinacion,
 * como en las referencias de la nave) y gira CON el spin — ver deathstar.h.
 * DS_DISH_LOCAL_AZ_DEG es su azimut de referencia en el frame del cuerpo
 * (con az local 0 y spin 0 el ojo mira exactamente a camara); DS_DISH_EL_DEG
 * es su elevacion, fija. */
#define DS_DISH_LOCAL_AZ_DEG 0.0f
#define DS_DISH_EL_DEG       20.0f

/* Velocidad de giro: ya no es constante (v5). DS_SPIN_RATE_DEG es la
 * velocidad de crucero, lejos del frente; DS_SPIN_RATE_MIN es la velocidad
 * justo al cruzar el frente — frenar ahi le da tiempo a la carga/disparo a
 * leerse en vez de que el ojo siga girando de largo mientras el rayo
 * converge. El ancho de la zona de frenado es ds->frontEaseDeg (v6): NO un
 * numero suelto, se calcula en deathstar_load a partir de DS_DISH_CULL_Z
 * (mismo angulo en que el ojo empieza a ser visible), asi la frenada y el
 * disparo (ver deathstar_update) arrancan exactamente cuando aparece, no en
 * un umbral ajustado a mano por separado que puede desincronizarse. */
#define DS_SPIN_RATE_DEG   48.0f
#define DS_SPIN_RATE_MIN    6.0f

/* Periodo del respawn incremental (antes venia de SECS). */
#define DS_RESPAWN_SECS 6.0f

/* Hundir el ojo en el cuerpo en vez de dejarlo apoyado sobre la superficie:
 * se coloca a un radio menor que worldR, asi la esfera tapa el borde del
 * plato con el z-test real (nada de sombreado truqueado) y se lee
 * encajado/concavo en vez de flotando pegado encima. Fraccion de worldR. */
#define DS_DISH_SINK_FRAC 0.03f

/* El bisel del socket (ver deathstar_render) se dibuja mas hundido todavia
 * que el ojo, no a la misma profundidad: si quedan coplanares, dos discos
 * casi pegados a la misma distancia de camara generan z-fighting (patron de
 * rayas parpadeando, visto en pruebas). Fraccion extra de worldR. */
#define DS_DISH_BEZEL_EXTRA_SINK 0.025f

/* El plato visto de canto degenera en una astilla oscura pegada al borde de
 * la silueta (confirmado tintandolo de rojo — era la "mancha gris" que se
 * reportaba). Sigue sin dibujarse por debajo de DS_DISH_CULL_Z (mismo
 * umbral que antes evitaba la astilla), pero ya no aparece/desaparece de
 * golpe ahi: se desvanece en alpha a lo largo de [CULL_Z, FADE_Z] de
 * dishPos.z/worldR, así que el pop al dar la vuelta ya no se nota. */
#define DS_DISH_CULL_Z 0.38f
#define DS_DISH_FADE_Z 0.62f

/* El objetivo del disparo ya no es uniforme en toda la pantalla: eso lo
 * dejaba caer a veces muy cerca del centro (la propia nave), y un rayo tan
 * corto se ve mal / no da sensacion de alcance. Se fuerza una distancia
 * minima al centro de pantalla (fraccion del lado corto), con resampleo. */
#define DS_AIM_MIN_DIST_FRAC 0.32f

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

/* Velocidad de giro en funcion del spin actual: crucero lejos del frente,
 * frenada suave (smoothstep) desde ds->frontEaseDeg (el mismo angulo en que
 * el ojo se vuelve visible, ver deathstar_load) — ver DS_SPIN_RATE_* arriba. */
static float ds_spin_rate_deg(float spin, float easeDeg)
{
    const float distFront = fminf(spin, 360.0f - spin); /* 0 en el frente */
    float t = ds_clampf(distFront / easeDeg, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t); /* smoothstep: sin quiebre al entrar/salir */
    return DS_SPIN_RATE_MIN + (DS_SPIN_RATE_DEG - DS_SPIN_RATE_MIN) * t;
}

/* Fraccion de visibilidad del ojo (0 = invisible/culled, 1 = alpha completo)
 * segun su profundidad actual — ver DS_DISH_CULL_Z/DS_DISH_FADE_Z. Unica
 * fuente de verdad: la usan tanto el render (para dibujarlo) como el update
 * (para frenar y disparar apenas aparece), asi no se desincronizan. */
static float ds_dish_fade(const DeathStar *ds)
{
    const float zRatio = ds->dishPos.z / ds->worldR;
    return ds_clampf(
        (zRatio - DS_DISH_CULL_Z) / (DS_DISH_FADE_Z - DS_DISH_CULL_Z), 0.0f, 1.0f);
}

/* Punto de impacto al azar, pero lejos del centro de pantalla (donde esta la
 * estacion) y del lado de pantalla donde esta el ojo ahora mismo (rightHalf
 * — ver el trigger en deathstar_update, que decide el lado por construccion
 * en vez de proyectar la posicion real del ojo): resampleo simple hasta que
 * cae fuera de un radio minimo del centro, con tope de intentos. El lado es
 * una restriccion dura del rango de muestreo; la distancia minima es
 * best-effort (resampleo, no garantizado) — a proposito, son dos cosas
 * distintas. */
static void ds_pick_aim(Rng *rng, float screenW, float screenH, int rightHalf,
                        float *outX, float *outY)
{
    const float cx = screenW * 0.5f, cy = screenH * 0.5f;
    const float minDist = fminf(screenW, screenH) * DS_AIM_MIN_DIST_FRAC;
    const float loX = rightHalf ? cx : 0.0f;
    const float hiX = rightHalf ? screenW : cx;
    /* Fallback si se agotan los intentos: al 25%/75% del ancho, nunca en el
     * centro exacto (que si violaria el lado pedido). */
    float x = screenW * (rightHalf ? 0.75f : 0.25f);
    float y = cy;
    for (int tries = 0; tries < 20; ++tries) {
        x = rng_range(rng, loX, hiX);
        y = rng_range(rng, 0.0f, screenH);
        const float dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy >= minDist * minDist) {
            break;
        }
    }
    *outX = x;
    *outY = y;
}

/* --- texturas procedurales -------------------------------------------------
 * Sin archivos de asset: todo con GenImageColor + Image*. */

/* Cuerpo: los ejes de textura de GenMeshSphere, verificados EMPIRICAMENTE
 * con los polos ya fijos arriba/abajo (pre-rotacion de 90 en X en
 * deathstar_load — sin eso los polos miraban a camara y cualquier prueba de
 * ejes era ambigua porque la esfera tumbaba en vez de girar; de ahi los
 * flip-flops de rondas anteriores). Prueba definitiva: banda X-constante
 * roja + banda Y-constante azul, dos capturas a angulos distintos:
 *   - X constante -> anillo HORIZONTAL a latitud fija, estable con el giro
 *     (la roja quedo cruzando el ecuador igual en ambas capturas).
 *   - Y constante -> MERIDIANO polo a polo, gira con el spin (la azul se
 *     movio y se oculto detras).
 * Por eso:
 *   - trinchera ecuatorial -> banda de X constante en texW/2 (queda como
 *     anillo horizontal estable, ya no barre configuraciones de canto).
 *   - paneles verticales (meridianos) -> lineas de Y constante.
 *   - paneles horizontales (paralelos) -> lineas de X constante. */
static Texture2D ds_build_skin(Rng *rng)
{
    const int texW = 512, texH = 256;
    Image img = GenImageColor(texW, texH, (Color){ 150, 150, 156, 255 });

    /* Meridianos (verticales sobre la esfera) + paralelos (horizontales). */
    for (int y = 0; y < texH; y += 16) {
        ImageDrawLine(&img, 0, y, texW - 1, y, (Color){ 92, 92, 100, 255 });
    }
    for (int x = 0; x < texW; x += 32) {
        ImageDrawLine(&img, x, 0, x, texH - 1, (Color){ 92, 92, 100, 255 });
    }

    /* Trinchera ecuatorial: anillo a latitud fija = banda de X constante. */
    const int eqX  = texW / 2;
    const int half = 8;
    ImageDrawRectangle(&img, eqX - half, 0, half * 2, texH, (Color){ 68, 68, 76, 255 });
    ImageDrawLine(&img, eqX, 0, eqX, texH - 1, (Color){ 36, 36, 42, 255 });

    /* Luces amarillas por toda la esfera. */
    for (int i = 0; i < 140; ++i) {
        const int lx = (int)rng_range(rng, 4.0f, (float)(texW - 4));
        const int ly = (int)rng_range(rng, 0.0f, (float)texH);
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

/* Reposiciona el ojo segun ds->spin: esta fijo a un punto del cuerpo, asi que
 * su azimut mundial es su azimut local mas la rotacion actual. Se llama cada
 * frame (el cuerpo gira, el ojo gira con el). */
static void ds_place_dish(DeathStar *ds)
{
    const float az = (DS_DISH_LOCAL_AZ_DEG + ds->spin) * DEG2RAD;
    const float el = DS_DISH_EL_DEG * DEG2RAD;

    Vector3 n = {
        sinf(az) * cosf(el),
        sinf(el),
        cosf(az) * cosf(el)
    };
    n = Vector3Normalize(n);
    ds->dishPos = Vector3Scale(n, ds->worldR * (1.0f - DS_DISH_SINK_FRAC));

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

void deathstar_load(DeathStar *ds, Rng *rng)
{
    ds->cam.position   = (Vector3){ 0.0f, 0.0f, 6.0f };
    ds->cam.target     = (Vector3){ 0.0f, 0.0f, 0.0f };
    ds->cam.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    ds->cam.fovy       = 45.0f;
    ds->cam.projection = CAMERA_PERSPECTIVE;

    /* worldR no depende de la resolucion: GetWorldToScreen/el proyector de
     * raylib cubren el alto completo de la ventana con fovy en Y sin
     * importar el ancho, asi que este radio ocupa siempre la misma fraccion
     * de pantalla y el resize no toca nada. Bajado de 0.30 a 0.19 (v6): se
     * veia demasiado grande frente a los sistemas solares (circulos de radio
     * chico); mas chica se lee como una nave mas en la escena, no como el
     * centro dominante. */
    const float frac = 0.19f;
    ds->worldR = frac * ds->cam.position.z * tanf(ds->cam.fovy * 0.5f * DEG2RAD);
    ds->dishR  = ds->worldR * 0.22f;
    const float dishH = ds->dishR * 0.16f;

    /* Angulo (grados de spin, a cada lado del frente) en que el ojo empieza
     * a ser visible: el mismo umbral que usa ds_dish_fade para el alpha
     * (DS_DISH_CULL_Z), pero despejado en grados una sola vez aca en vez de
     * recalculado cada frame — lo usan ds_spin_rate_deg (frenar) y
     * deathstar_update (disparar) para que ambos arranquen exactamente
     * cuando aparece, ni antes ni despues (v6, pedido explicito). Formula:
     * dishPos.z/worldR = (1-sink)*cos(el)*cos(spin) (ver ds_place_dish), asi
     * que cos(spin) = CULL_Z / ((1-sink)*cos(el)). */
    {
        const float denom = (1.0f - DS_DISH_SINK_FRAC) * cosf(DS_DISH_EL_DEG * DEG2RAD);
        const float c = ds_clampf(DS_DISH_CULL_Z / denom, -1.0f, 1.0f);
        ds->frontEaseDeg = acosf(c) * RAD2DEG;
    }

    ds->skin = ds_build_skin(rng);
    ds->body = LoadModelFromMesh(GenMeshSphere(ds->worldR, 24, 32));
    SetMaterialTexture(&ds->body.materials[0], MATERIAL_MAP_DIFFUSE, ds->skin);

    /* GenMeshSphere trae los polos sobre el eje Z (mirando a camara), asi
     * que girar sobre Y hacia TUMBAR la esfera en vez de girarla como
     * planeta: el polo barria el frente y los bordes, y la trinchera pasaba
     * por configuraciones de canto que se veian como una cuna oscura
     * apuntando al centro (la "mancha gris" reportada, confirmada en el
     * barrido con tinte). model.transform se aplica ANTES de la rotacion de
     * DrawModelEx, asi que esta pre-rotacion deja los polos arriba/abajo de
     * una vez por todas y el spin sobre Y queda como giro de planeta. */
    ds->body.transform = MatrixRotate((Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f * DEG2RAD);

    ds->dishSkin = ds_build_dish_skin();
    ds->dish     = LoadModelFromMesh(GenMeshCylinder(ds->dishR, dishH, 24));
    SetMaterialTexture(&ds->dish.materials[0], MATERIAL_MAP_DIFFUSE, ds->dishSkin);

    ds->spin = 0.0f;
    ds_place_dish(ds);

    ds->aimX  = 0.0f;
    ds->aimY  = 0.0f;
    ds->blastR = 90.0f; /* deathstar_update lo reescala con ss->cellR en cuanto corre */

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
    ds->timer        = 0.0f; /* solo CHARGE/FIRE lo usan */
    ds->respawnTimer = DS_RESPAWN_SECS;
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
    /* Frenar apenas el ojo se vuelve visible, no al llegar al frente exacto
     * (v6): flanco de subida de ds_dish_fade, medido ANTES de mover el ojo
     * (dishPos todavia tiene la posicion del frame anterior) y DESPUES (ya
     * con el spin nuevo aplicado) — asi el cruce nunca se pierde aunque un
     * frame lento salte de golpe. La velocidad de giro no es constante
     * (ds_spin_rate_deg): se frena desde ds->frontEaseDeg, el mismo umbral
     * de aparicion.
     *
     * Dos disparos por vuelta (v7, pedido explicito): uno apenas aparece
     * (justAppeared, arriba) y otro al cruzar el frente exacto (atMiddle,
     * capturado ANTES de envolver el spin — mismo patron que v4/v5 usaban
     * para "atFront"). El lado de pantalla al que apunta cada uno sale de
     * CUAL disparo es, no de proyectar ds->dishPos: el disparo de aparicion
     * ocurre siempre en spin=360-frontEaseDeg (rango (180,360), sin(spin)<0
     * -> el ojo esta del lado -X del mundo -> pantalla-izquierda, porque
     * esta camara mira desde +Z con up=+Y, asi que +X mundo es
     * pantalla-derecha) y el de cruce de frente ocurre siempre justo
     * despues del envolvido, con spin pequeno y positivo (sin(spin)>=0 ->
     * pantalla-derecha). No se usa GetWorldToScreen para esto a proposito:
     * ademas de ser redundante (en el cruce el ojo esta en x~0, ambiguo),
     * GetWorldToScreen llama a GetScreenWidth() internamente, que devuelve
     * 0 sin InitWindow — romperia el arnes headless (ver STATUS.md) que
     * corre deathstar_update sin GPU/ventana. */
    const float fadeBefore = ds_dish_fade(ds);
    float rawSpin = ds->spin + ds_spin_rate_deg(ds->spin, ds->frontEaseDeg) * dt;
    const int atMiddle = (rawSpin >= 360.0f);
    if (atMiddle) {
        rawSpin -= 360.0f;
    }
    ds->spin = rawSpin;
    ds_place_dish(ds); /* el ojo esta fijo al cuerpo: se reubica cada frame */
    const float fadeAfter = ds_dish_fade(ds);
    const int justAppeared = (fadeBefore <= 0.0f && fadeAfter > 0.0f);

    /* El radio de impacto se ata a la escala real de los sistemas (cellR ya
     * refleja el tamano de celda para el N actual), no a un pixel fijo que se
     * sentiria enorme con N chico o inutil con N grande. */
    if (ss->cellR > 1.0f) {
        ds->blastR = ss->cellR * 1.3f;
    }

    if ((justAppeared || atMiddle) && ds->phase == DS_IDLE) {
        /* Ojo recien aparecido (izquierda) o cruzando el frente (derecha):
         * objetivo al azar, lejos del centro y del lado que toque (ver
         * ds_pick_aim arriba). Sin overlap posible entre los dos disparos:
         * CHARGE+FIRE dura 1.25s y el transito de aparicion a frente son
         * ~3.8s (integrando ds_spin_rate_deg), asi que el guard de
         * phase==DS_IDLE de aqui abajo nunca hace falta reforzarlo — en el
         * peor caso un frame raro se saltaria un disparo, no corrompe nada. */
        ds_pick_aim(rng, screenW, screenH, atMiddle, &ds->aimX, &ds->aimY);
        ds->phase = DS_CHARGE;
        ds->timer = DS_CHARGE_SECS;
    }

    switch (ds->phase) {
    case DS_IDLE:
        break; /* espera a que el ojo vuelva a aparecer, arriba */

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
            ds->phase = DS_IDLE; /* la proxima vez que el ojo aparezca dispara solo */
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
            ds->respawnTimer = DS_RESPAWN_SECS;
        }
    } else {
        ds->respawnTimer = DS_RESPAWN_SECS;
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

/* Explosion por capas, toda analitica (posicion = f(edad), sin estado extra:
 * la variacion por particula se deriva de partDir/partSpd ya existentes):
 *   1. destello inicial con puntas de difraccion (mismo truco visual que las
 *      estrellas brillantes de render_starfield), muere rapido;
 *   2. doble onda expansiva (choque + termica, velocidades distintas);
 *   3. nucleo de fuego que decae;
 *   4. particulas con desaceleracion (no lineales) y color por velocidad:
 *      las rapidas salen blanco-amarillas, las lentas quedan como brasas
 *      naranjas/rojizas al final, como en una explosion real;
 *   5. humo gris residual en la segunda mitad (BLEND_ALPHA, no aditivo),
 *      para que el final no sea un corte seco. */
static void ds_render_explosions(const DeathStar *ds)
{
    if (ds->expCount == 0) {
        return;
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < ds->expCount; ++i) {
        const float t = ds_clampf(ds->expAge[i] / ds->expDur[i], 0.0f, 1.0f);
        const Vector2 c = { ds->expX[i], ds->expY[i] };

        /* 1. destello + puntas de difraccion */
        if (t < 0.22f) {
            const float ft  = 1.0f - t / 0.22f; /* 1 -> 0 */
            const float len = 90.0f * ft;
            const Color spike = { 255, 240, 200, ds_alpha8(0.8f * ft) };
            DrawCircleV(c, 6.0f + 26.0f * ft, (Color){ 255, 255, 235, ds_alpha8(0.9f * ft) });
            DrawLineEx((Vector2){ c.x - len, c.y }, (Vector2){ c.x + len, c.y }, 2.0f, spike);
            DrawLineEx((Vector2){ c.x, c.y - len }, (Vector2){ c.x, c.y + len }, 2.0f, spike);
        }

        /* 2. doble onda expansiva */
        DrawCircleLines((int)c.x, (int)c.y, t * DS_SHOCK_MAXR,
                        (Color){ 255, 200, 120, ds_alpha8((1.0f - t) * 0.7f) });
        DrawCircleLines((int)c.x, (int)c.y, t * DS_SHOCK_MAXR * 0.6f,
                        (Color){ 255, 150, 80, ds_alpha8((1.0f - t) * 0.45f) });

        /* 3. nucleo de fuego */
        DrawCircleV(c, (1.0f - t) * (1.0f - t) * 30.0f + 3.0f,
                   (Color){ 255, 190, 110, ds_alpha8((1.0f - t) * 0.8f) });

        /* 4. particulas: desaceleracion cuadratica + color por velocidad */
        const float ease = 1.0f - (1.0f - t) * (1.0f - t); /* 0->1 frenando */
        for (int k = 0; k < DS_EXP_PARTICLES; ++k) {
            const int   idx  = i * DS_EXP_PARTICLES + k;
            const float spd  = ds->partSpd[idx];
            const float dist = spd * ds->expDur[i] * 0.55f * ease;
            const float hot  = ds_clampf((spd - 60.0f) / 160.0f, 0.0f, 1.0f);
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * dist,
                c.y + sinf(ds->partDir[idx]) * dist
            };
            DrawCircleV(p, (1.0f - t) * 2.4f + 0.5f,
                       (Color){ 255,
                                (unsigned char)(120.0f + 135.0f * hot),
                                (unsigned char)(60.0f + 150.0f * hot),
                                ds_alpha8((1.0f - t) * 0.9f) });
        }
    }
    EndBlendMode();

    /* 5. humo residual (alpha normal: es opaco-gris, no luz) */
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < ds->expCount; ++i) {
        const float t = ds_clampf(ds->expAge[i] / ds->expDur[i], 0.0f, 1.0f);
        if (t <= 0.45f) {
            continue;
        }
        const float st = (t - 0.45f) / 0.55f; /* 0 -> 1 */
        const Vector2 c = { ds->expX[i], ds->expY[i] };
        for (int k = 0; k < 3; ++k) {
            const int   idx = i * DS_EXP_PARTICLES + k;
            const float off = 16.0f + 9.0f * (float)k;
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * off,
                c.y + sinf(ds->partDir[idx]) * off
            };
            DrawCircleV(p, 14.0f + 26.0f * st,
                       (Color){ 90, 88, 86, ds_alpha8((1.0f - st) * 0.30f) });
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
    /* Desvanecido en vez de aparecer/desaparecer de golpe (ds_dish_fade,
     * misma funcion que usa deathstar_update para frenar/disparar): alpha 0
     * por debajo del umbral que evitaba la astilla de canto (nunca se ve esa
     * geometria degenerada), 1 por encima de la zona de transicion. */
    const float dishFade = ds_dish_fade(ds);
    if (dishFade > 0.0f) {
        const Vector3 nrm = Vector3Normalize(ds->dishPos);
        BeginBlendMode(BLEND_ALPHA);
        /* Bisel del socket: el mismo modelo del ojo, mas ancho (radio x1.6
         * en su plano, misma altura) y oscuro/traslucido, un poco mas
         * hundido que el ojo (DS_DISH_BEZEL_EXTRA_SINK, evita z-fighting
         * con el ojo real) — la esfera recorta su borde con el z-test real,
         * asi que se ve como un cerco oscuro rodeando el ojo en vez del ojo
         * flotando pegado encima de la superficie. */
        const Vector3 bezelPos = Vector3Scale(nrm,
            ds->worldR * (1.0f - DS_DISH_SINK_FRAC - DS_DISH_BEZEL_EXTRA_SINK));
        DrawModelEx(ds->dish, bezelPos, ds->dishRotAxis, ds->dishRotAngle,
                   (Vector3){ 1.6f, 1.0f, 1.6f },
                   (Color){ 10, 12, 10, ds_alpha8(0.65f * dishFade) });
        DrawModelEx(ds->dish, ds->dishPos, ds->dishRotAxis, ds->dishRotAngle,
                   (Vector3){ 1.0f, 1.0f, 1.0f },
                   (Color){ 255, 255, 255, ds_alpha8(dishFade) });
        EndBlendMode();
    }

    EndMode3D();

    ds_render_beam(ds);
    ds_render_explosions(ds);
}
