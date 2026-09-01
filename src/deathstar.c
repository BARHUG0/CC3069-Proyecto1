#include "deathstar.h"

#include <math.h>

#include "raymath.h"

#define DS_TWO_PI      6.2831853f
#define DS_CHARGE_SECS 0.9f
#define DS_FIRE_SECS   0.35f
/* Duracion mas larga (era 1.6s) para que la explosion se sienta mas suave,
 * no de golpe — pedido explicito. Cada fase de ds_render_explosions esta
 * en funcion de t=edad/duracion, asi que alargar esta constante estira
 * todas las fases (destello, onda, nucleo, particulas, humo) sin retocar
 * cada una a mano. La UNICA fase que NO debia estirarse sola era el
 * alcance de las particulas (ver DS_EXP_PART_REACH mas abajo). */
#define DS_EXP_DUR     2.6f
/* Radio maximo de la onda expansiva, subido junto con la duracion para que
 * la onda tenga espacio de sentirse "expandiendose" en vez de llegar a su
 * tamano final muy rapido contra la nueva linea de tiempo, mas larga. */
#define DS_SHOCK_MAXR  170.0f
/* Alcance de las particulas: ANTES `dist = spd * ds->expDur[i] * 0.55 *
 * ease` multiplicaba por la duracion de LA PROPIA explosion — con
 * DS_EXP_DUR mas largo eso mandaria las particulas ~1.6x mas lejos sin
 * querer, separandolas visualmente de la onda expansiva (que no escala con
 * la duracion, siempre llega a DS_SHOCK_MAXR). Esta constante reproduce la
 * distancia real de antes (1.6*0.55=0.88) pero SIN depender de expDur, asi
 * el alcance queda igual sin importar cuanto dure la explosion — solo el
 * RITMO (via `ease`, que si esta normalizado por duracion) se estira. */
#define DS_EXP_PART_REACH 0.88f

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
    /* Casco mas oscuro que antes (era {150,150,156}, casi gris palido) pero
     * sin llegar a negro — pedido explicito: "mas oscuro pero no como
     * negro". Los paneles/trinchera se retuvieron mas abajo por el mismo
     * ancho de contraste que antes tenian contra su base, asi siguen
     * legibles en vez de lavarse contra el casco mas oscuro. */
    Image img = GenImageColor(texW, texH, (Color){ 78, 80, 88, 255 });

    /* Meridianos (verticales sobre la esfera) + paralelos (horizontales). */
    for (int y = 0; y < texH; y += 16) {
        ImageDrawLine(&img, 0, y, texW - 1, y, (Color){ 112, 115, 125, 255 });
    }
    for (int x = 0; x < texW; x += 32) {
        ImageDrawLine(&img, x, 0, x, texH - 1, (Color){ 112, 115, 125, 255 });
    }

    /* Trinchera ecuatorial: anillo a latitud fija = banda de X constante. */
    const int eqX  = texW / 2;
    const int half = 8;
    ImageDrawRectangle(&img, eqX - half, 0, half * 2, texH, (Color){ 46, 45, 52, 255 });
    ImageDrawLine(&img, eqX, 0, eqX, texH - 1, (Color){ 22, 21, 26, 255 });

    /* Luces por toda la esfera: mas cantidad que antes (140->260) y en 3
     * tonos calidos en vez de uno solo (blanco-amarillo caliente, ambar,
     * naranja profundo, elegidos al azar por luz) — mas vivido y mas denso
     * a la vez, contra el casco ahora mas oscuro se leen mejor. */
    static const Color LIGHT_TONES[3] = {
        { 255, 240, 190, 255 }, /* blanco-amarillo caliente */
        { 255, 214, 110, 255 }, /* ambar (tono original)    */
        { 255, 160,  70, 255 }  /* naranja profundo         */
    };
    for (int i = 0; i < 260; ++i) {
        const int lx = (int)rng_range(rng, 4.0f, (float)(texW - 4));
        const int ly = (int)rng_range(rng, 0.0f, (float)texH);
        const uint32_t tone = rng_below(rng, 3u);
        ImageDrawCircle(&img, lx, ly, 1, LIGHT_TONES[tone]);
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
    ds->victim       = -1;
}

void deathstar_center(DeathStar *ds, float screenW, float screenH)
{
    ds->posX = screenW * 0.5f;
    ds->posY = screenH * 0.5f;
}

void deathstar_move(DeathStar *ds, float moveX, float moveY, float screenW, float screenH,
                    float dt)
{
    const float length = sqrtf(moveX * moveX + moveY * moveY);
    if (length > 0.0f) {
        moveX /= length;
        moveY /= length;
        ds->posX += moveX * DS_MOVE_PX_S * dt;
        ds->posY += moveY * DS_MOVE_PX_S * dt;
    }

    const float visibleH = 2.0f * ds->cam.position.z * tanf(ds->cam.fovy * 0.5f * DEG2RAD);
    const float radiusPx = ds->worldR * screenH / visibleH;
    ds->posX = ds_clampf(ds->posX, radiusPx, screenW - radiusPx);
    ds->posY = ds_clampf(ds->posY, radiusPx, screenH - radiusPx);
}

int deathstar_fire(DeathStar *ds, const SolarSystems *ss, Rng *rng)
{
    if (ds->phase != DS_IDLE || ss->count <= 0) {
        return 0;
    }

    ds->victim = (int)rng_below(rng, (uint32_t)ss->count);
    ds->aimX = ss->cx[ds->victim];
    ds->aimY = ss->cy[ds->victim];
    ds->phase = DS_CHARGE;
    ds->timer = DS_CHARGE_SECS;
    return 1;
}

/* --- explosiones ----------------------------------------------------------
 * Tabla aplanada: la explosion i ocupa
 * [i*DS_EXP_PARTICLES, (i+1)*DS_EXP_PARTICLES) en partDir/partSpd. */
static void ds_spawn_explosion(DeathStar *ds, Rng *rng, float cx, float cy, int layer)
{
    if (ds->expCount >= DS_MAX_EXPLOSIONS) {
        return; /* explosiones simultaneas de sobra para el ritmo de disparo */
    }
    const int i = ds->expCount++;
    ds->expX[i]     = cx;
    ds->expY[i]     = cy;
    ds->expAge[i]   = 0.0f;
    ds->expDur[i]   = DS_EXP_DUR;
    ds->expLayer[i] = layer;

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
            ds->expX[i]     = ds->expX[last];
            ds->expY[i]     = ds->expY[last];
            ds->expAge[i]   = ds->expAge[last];
            ds->expDur[i]   = ds->expDur[last];
            ds->expLayer[i] = ds->expLayer[last];
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
    (void)screenW;
    (void)screenH;

    float rawSpin = ds->spin + ds_spin_rate_deg(ds->spin, ds->frontEaseDeg) * dt;
    if (rawSpin >= 360.0f) {
        rawSpin -= 360.0f;
    }
    ds->spin = rawSpin;
    ds_place_dish(ds); /* el ojo esta fijo al cuerpo: se reubica cada frame */

    /* El radio de impacto se ata a la escala real de los sistemas (cellR ya
     * refleja el tamano de celda para el N actual), no a un pixel fijo que se
     * sentiria enorme con N chico o inutil con N grande. */
    if (ss->cellR > 1.0f) {
        ds->blastR = ss->cellR * 1.3f;
    }

    if (ds->phase == DS_CHARGE && ds->victim >= 0 && ds->victim < ss->count) {
        ds->aimX = ss->cx[ds->victim];
        ds->aimY = ss->cy[ds->victim];
    }

    switch (ds->phase) {
    case DS_IDLE:
        break;

    case DS_CHARGE:
        ds->timer -= dt;
        if (ds->timer <= 0.0f) {
            ds->phase = DS_FIRE;
            ds->timer = DS_FIRE_SECS;

            const int victim = ds->victim;
            if (victim >= 0 && victim < ss->count) {
                ds_spawn_explosion(ds, rng, ss->cx[victim], ss->cy[victim], ss->layer[victim]);
                trails_drop_system(tb, ss, victim);
                solar_system_remove(w, ss, victim);
                ds->kills++;
            }
            ds->victim = -1;
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
static Vector3 ds_world_offset(const DeathStar *ds, int screenW, int screenH)
{
    const float visibleH = 2.0f * ds->cam.position.z * tanf(ds->cam.fovy * 0.5f * DEG2RAD);
    const float worldPerPixel = visibleH / (float)screenH;
    return (Vector3){
        (ds->posX - (float)screenW * 0.5f) * worldPerPixel,
        ((float)screenH * 0.5f - ds->posY) * worldPerPixel,
        0.0f
    };
}

static void ds_render_beam(const DeathStar *ds, Vector3 offset)
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

    const Vector3 dishWorld = Vector3Add(ds->dishPos, offset);
    const Vector2 dishScreen = GetWorldToScreen(dishWorld, ds->cam);

    const float chargeT = (ds->phase == DS_FIRE) ? 1.0f
        : ds_clampf(1.0f - ds->timer / DS_CHARGE_SECS, 0.0f, 1.0f);

    Vector2 rim[8];
    for (int i = 0; i < 8; ++i) {
        const float theta = (float)i * (DS_TWO_PI / 8.0f);
        const Vector3 p = Vector3Add(dishWorld,
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
 *   2. una sola onda expansiva, sutil (banda fina, alpha bajo) — apoya a la
 *      bola de fuego y las particulas, no compite con ellas como forma
 *      propia. Antes eran DOS bandas concentricas mas un nucleo circular:
 *      tres formas perfectas centradas en el mismo punto se leian como un
 *      blanco/bullseye, no como una explosion — rechazado explicitamente;
 *   3. bola de fuego IRREGULAR: en vez de un unico circulo perfecto, un
 *      racimo de 6 circulos superpuestos, desplazados una fraccion chica y
 *      FIJA del radio actual (no crece con el tiempo, encoge junto con el
 *      nucleo) usando los mismos angulos que las primeras 6 particulas de
 *      abajo — mismo estilo (nada de estado nuevo ni sorteos extra), pero
 *      la silueta ya no es un disco perfecto;
 *   4. particulas con desaceleracion (no lineales) y color por velocidad:
 *      las rapidas salen blanco-amarillas, las lentas quedan como brasas
 *      naranja-rojizas vividas al final, como en una explosion real;
 *   5. humo gris residual en la segunda mitad (BLEND_ALPHA, no aditivo),
 *      para que el final no sea un corte seco.
 *
 * Todo el tamano y el alpha de las 5 fases se escala por sc/al —
 * solar_layer_scale/solar_layer_alpha de la CAPA del sistema que murio
 * (ds->expLayer[i], spawn.h) — para que la explosion de un sistema de una
 * capa de atras se vea tan chica y tenue como se veia el sistema vivo, en
 * vez de saltar siempre a tamano/brillo de primera capa. */
static void ds_render_explosions(const DeathStar *ds)
{
    if (ds->expCount == 0) {
        return;
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < ds->expCount; ++i) {
        const float t  = ds_clampf(ds->expAge[i] / ds->expDur[i], 0.0f, 1.0f);
        const float sc = solar_layer_scale(ds->expLayer[i]);
        const float al = solar_layer_alpha(ds->expLayer[i]);
        const Vector2 c = { ds->expX[i], ds->expY[i] };

        /* 1. destello + puntas de difraccion */
        if (t < 0.22f) {
            const float ft  = 1.0f - t / 0.22f; /* 1 -> 0 */
            const float len = 90.0f * ft * sc;
            const Color spike = { 255, 180, 70, ds_alpha8(0.85f * ft * al) };
            DrawCircleV(c, (6.0f + 26.0f * ft) * sc,
                       (Color){ 255, 250, 220, ds_alpha8(0.9f * ft * al) });
            DrawLineEx((Vector2){ c.x - len, c.y }, (Vector2){ c.x + len, c.y }, 2.0f, spike);
            DrawLineEx((Vector2){ c.x, c.y - len }, (Vector2){ c.x, c.y + len }, 2.0f, spike);
        }

        /* 2. onda expansiva unica y sutil: banda fina, alpha bajo. */
        const float outer = t * DS_SHOCK_MAXR * sc;
        const float width = 8.0f * sc * (1.0f - t * 0.5f);
        DrawRing(c, ds_clampf(outer - width, 0.0f, outer), outer, 0.0f, 360.0f, 48,
                (Color){ 255, 130, 40, ds_alpha8((1.0f - t) * 0.35f * al) });

        /* 3. bola de fuego irregular: racimo de circulos superpuestos, no
         * un disco perfecto. coreR es el mismo radio/decaimiento de antes;
         * cada puff se desplaza una fraccion FIJA de coreR (encoge junto
         * con el nucleo, no se dispersa como las particulas de abajo) y
         * varia de tamano segun k para que el contorno sea irregular. */
        const float coreR = ((1.0f - t) * (1.0f - t) * 30.0f + 3.0f) * sc;
        const Color fireColor = { 255, 140, 40, ds_alpha8((1.0f - t) * 0.8f * al) };
        for (int k = 0; k < 6; ++k) {
            const int   idx    = i * DS_EXP_PARTICLES + k;
            const float off    = coreR * 0.35f;
            const float puffR  = coreR * (0.55f + 0.18f * (float)(k % 3));
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * off,
                c.y + sinf(ds->partDir[idx]) * off
            };
            DrawCircleV(p, puffR, fireColor);
        }

        /* 4. particulas: desaceleracion cuadratica + color por velocidad.
         * DS_EXP_PART_REACH, no ds->expDur[i]: el alcance no debe crecer
         * solo porque la explosion dura mas (ver el comentario de la
         * constante, arriba) — si no, las particulas se adelantarian a la
         * onda expansiva de encima, que si tiene un radio maximo fijo. */
        const float ease = 1.0f - (1.0f - t) * (1.0f - t); /* 0->1 frenando */
        for (int k = 0; k < DS_EXP_PARTICLES; ++k) {
            const int   idx  = i * DS_EXP_PARTICLES + k;
            const float spd  = ds->partSpd[idx];
            const float dist = spd * DS_EXP_PART_REACH * ease * sc;
            const float hot  = ds_clampf((spd - 60.0f) / 160.0f, 0.0f, 1.0f);
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * dist,
                c.y + sinf(ds->partDir[idx]) * dist
            };
            DrawCircleV(p, ((1.0f - t) * 2.4f + 0.5f) * sc,
                       (Color){ 255,
                                (unsigned char)(90.0f + 165.0f * hot),
                                (unsigned char)(15.0f + 175.0f * hot),
                                ds_alpha8((1.0f - t) * 0.9f * al) });
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
        const float sc = solar_layer_scale(ds->expLayer[i]);
        const float al = solar_layer_alpha(ds->expLayer[i]);
        const float st = (t - 0.45f) / 0.55f; /* 0 -> 1 */
        const Vector2 c = { ds->expX[i], ds->expY[i] };
        for (int k = 0; k < 3; ++k) {
            const int   idx = i * DS_EXP_PARTICLES + k;
            const float off = (16.0f + 9.0f * (float)k) * sc;
            const Vector2 p = {
                c.x + cosf(ds->partDir[idx]) * off,
                c.y + sinf(ds->partDir[idx]) * off
            };
            DrawCircleV(p, (14.0f + 26.0f * st) * sc,
                       (Color){ 90, 88, 86, ds_alpha8((1.0f - st) * 0.30f * al) });
        }
    }
    EndBlendMode();
}

void deathstar_render(const DeathStar *ds, int screenW, int screenH)
{
    const Vector3 offset = ds_world_offset(ds, screenW, screenH);

    BeginMode3D(ds->cam);

    DrawModelEx(ds->body, offset, (Vector3){ 0.0f, 1.0f, 0.0f },
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
        const Vector3 bezelPos = Vector3Add(offset, Vector3Scale(nrm,
            ds->worldR * (1.0f - DS_DISH_SINK_FRAC - DS_DISH_BEZEL_EXTRA_SINK)));
        DrawModelEx(ds->dish, bezelPos, ds->dishRotAxis, ds->dishRotAngle,
                   (Vector3){ 1.6f, 1.0f, 1.6f },
                   (Color){ 10, 12, 10, ds_alpha8(0.65f * dishFade) });
        DrawModelEx(ds->dish, Vector3Add(offset, ds->dishPos),
                   ds->dishRotAxis, ds->dishRotAngle,
                   (Vector3){ 1.0f, 1.0f, 1.0f },
                   (Color){ 255, 255, 255, ds_alpha8(dishFade) });
        EndBlendMode();
    }

    EndMode3D();

    ds_render_beam(ds, offset);
    ds_render_explosions(ds);
}
