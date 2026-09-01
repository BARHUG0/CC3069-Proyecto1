/* deathstar.h - Modalidad opcional --deadstar: una Estrella de la Muerte 3D
 * controlada con WASD que dispara el superlaser con Space a un sistema solar
 * elegido al azar y lo destruye.
 *
 * Modelo real (GenMeshSphere + GenMeshCylinder, Camera3D), no primitivas 2D:
 * el cuerpo gira sobre su eje, y el "ojo" (plato del superlaser) es un
 * elemento aparte, chico, texturizado con un degradado radial para que se
 * lea cóncavo, hundido en el cuerpo (DS_DISH_SINK_FRAC) con un bisel oscuro
 * detrás (mismo modelo, más ancho y más hundido) para que se lea encajado
 * en un socket en vez de flotando pegado sobre la superficie. El ojo está
 * FIJO a un punto de la superficie del cuerpo (arriba, con una leve
 * inclinación) y gira CON la estación — no es independiente del spin, y su
 * velocidad de giro no es constante: se frena en cuanto el ojo empieza a
 * aparecer (ds_spin_rate_deg, umbral ds->frontEaseDeg — el mismo angulo en
 * que arranca el desvanecido de visibilidad, no uno ajustado aparte).
 * La rotacion nunca se detiene del todo y solo se frena cerca del frente.
 * El disparo se inicia manualmente y el objetivo permanece ligado al sistema
 * elegido mientras carga. El rayo sale
 * de donde esté el ojo hacia el punto de impacto real (proyectado con
 * GetWorldToScreen, pero solo en el render — deathstar_update nunca
 * proyecta nada, para poder correr sin GPU/ventana en el arnes de pruebas).
 * De canto el ojo se desvanece en alpha en vez de aparecer/desaparecer de
 * golpe (DS_DISH_CULL_Z/DS_DISH_FADE_Z).
 *
 * Mismo estilo del resto del ECS: nada de metodos ni struct por entidad. El
 * estado de la estacion es un solo struct (SoA para las explosiones, igual
 * que la tabla de anillos de SolarSystems) que vive fuera del World, en la
 * misma categoria que StarField/TrailBuffer (systems.h): no es un dato por
 * entidad, es estado global de un efecto.
 *
 * Las particulas de explosion NO son entidades del World a proposito: su
 * posicion se calcula analiticamente (centro + direccion*velocidad*edad), asi
 * que no hace falta ni un par vx/vy nuevo en World ni un sistema de
 * integracion. Ademas evita destruirlas con C_LIFE, que main.c usa para
 * descontar sf.liveStars — una particula ahi descontaria estrellas que nunca
 * existieron.
 */
#ifndef DEATHSTAR_H
#define DEATHSTAR_H

#include "raylib.h"

#include "ecs.h"
#include "rng.h"
#include "spawn.h"
#include "systems.h"

#define DS_MAX_EXPLOSIONS   8
#define DS_EXP_PARTICLES   24
#define DS_MOVE_PX_S       320.0f

enum DeathStarPhase {
    DS_IDLE = 0,   /* esperando el proximo disparo               */
    DS_CHARGE,     /* apuntando, los 8 haces del borde se cargan  */
    DS_FIRE        /* rayo visible; el impacto se resuelve al entrar */
};

typedef struct DeathStar {
    /* --- estacion 3D --- */
    Model     body;     /* GenMeshSphere texturizada                */
    Model     dish;     /* GenMeshCylinder achatado, el "ojo"        */
    Texture2D skin;      /* textura procedural del cuerpo             */
    Texture2D dishSkin;  /* degradado radial del plato (profundidad)  */
    Camera3D  cam;
    float     spin;      /* rotacion del cuerpo (grados), eje Y       */
    float     worldR;    /* radio del cuerpo en unidades de mundo     */
    float     dishR;     /* radio del plato en unidades de mundo      */
    float     frontEaseDeg; /* grados de spin (a cada lado del frente)
                             * donde el ojo es visible (ver DS_DISH_CULL_Z);
                             * calculado en deathstar_load, no a ojo — la
                             * frenada y el disparo usan este mismo umbral. */

    /* Posicion/orientacion del plato. Se recalculan CADA FRAME a partir de
     * ds->spin: el ojo esta fijo a un punto del cuerpo y gira con el. */
    Vector3 dishPos;
    Vector3 dishRotAxis;
    float   dishRotAngle; /* grados */

    /* --- maquina de disparo --- */
    int   phase;
    float timer;      /* cuenta atras de CHARGE/FIRE (s); IDLE no usa timer:
                       * el disparo lo dispara el cruce del ojo por el frente */
    float aimX, aimY; /* objetivo actual, en pixeles de pantalla    */
    float blastR;     /* radio de impacto, en pixeles               */
    int   kills;
    int   victim;     /* sistema elegido para destruir, -1 = ninguno */
    float posX, posY; /* centro de la estacion en pantalla             */

    /* Respawn incremental: si count < targetN, cada `interval` nace un
     * sistema nuevo, para que la poblacion nunca llegue a cero. */
    float respawnTimer;

    /* --- explosiones: SoA + tabla de particulas aplanada, mismo patron que
     * ringFirst/planetCount sobre la tabla de anillos (spawn.h) pero con
     * rango fijo (DS_EXP_PARTICLES por explosion, no hace falta ringFirst). */
    int   expCount;
    float expX[DS_MAX_EXPLOSIONS], expY[DS_MAX_EXPLOSIONS];
    float expAge[DS_MAX_EXPLOSIONS], expDur[DS_MAX_EXPLOSIONS];
    /* Capa de profundidad (ver SYS_LAYER_COUNT, spawn.h) del sistema que
     * murio para crear esta explosion, no una escala/alpha precalculada:
     * ds_render_explosions llama a solar_layer_scale/solar_layer_alpha con
     * esto, misma fuente de verdad que usa el resto del render por capas,
     * para que la explosion se vea tan chica/tenue como se veia el sistema
     * vivo. */
    int   expLayer[DS_MAX_EXPLOSIONS];
    float partDir[DS_MAX_EXPLOSIONS * DS_EXP_PARTICLES];
    float partSpd[DS_MAX_EXPLOSIONS * DS_EXP_PARTICLES];
} DeathStar;

/* Construye modelos y texturas. Llamar tras InitWindow (GPU ya lista). */
void deathstar_load(DeathStar *ds, Rng *rng);
void deathstar_unload(DeathStar *ds); /* antes de CloseWindow */

/* Reinicia la maquina de disparo y las explosiones activas, sin recrear
 * modelos. Se llama junto con build_scene (tecla R, cambio de resolucion). */
void deathstar_reset(DeathStar *ds);

/* Avanza rotacion, resuelve el disparo activo, explosiones y respawn. */
void deathstar_update(DeathStar *ds, World *w, SolarSystems *ss, TrailBuffer *tb,
                      Rng *rng, int targetN, float screenW, float screenH, float dt);

void deathstar_center(DeathStar *ds, float screenW, float screenH);
void deathstar_move(DeathStar *ds, float moveX, float moveY, float screenW, float screenH,
                    float dt);
int deathstar_fire(DeathStar *ds, const SolarSystems *ss, Rng *rng);

/* Dibuja la estacion (3D), el rayo y las explosiones (2D, proyectados con
 * GetWorldToScreen). Debe correr entre BeginDrawing/EndDrawing, despues de
 * sys_render: la estacion tapa a los sistemas que le pasan por detras. */
void deathstar_render(const DeathStar *ds, int screenW, int screenH);

#endif /* DEATHSTAR_H */
