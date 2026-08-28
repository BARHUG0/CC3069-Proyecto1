/* deathstar.h - Modalidad opcional --deadstar: una Estrella de la Muerte 3D
 * en el centro de la pantalla que dispara el superlaser cada cierto tiempo a
 * un punto al azar; si le pega a un sistema solar, lo destruye.
 *
 * Modelo real (GenMeshSphere + GenMeshCylinder, Camera3D), no primitivas 2D:
 * el cuerpo gira sobre su eje, y el "ojo" (plato del superlaser) es un
 * elemento aparte, chico, texturizado con un degradado radial para que se
 * lea cóncavo. El ojo está FIJO a un punto de la superficie del cuerpo
 * (arriba, con una leve inclinación) y gira CON la estación — no es
 * independiente del spin. Dispara UNA vez por vuelta: exactamente cuando el
 * ojo pasa por el frente (mirando a cámara, cruce de 360°→0° del spin), a un
 * punto al azar de toda la pantalla. No hay cronometro de disparo ni valor
 * SECS en la bandera: el ritmo lo fija la velocidad de rotación
 * (DS_SPIN_RATE_DEG). El rayo sale de donde esté el ojo hacia el punto de
 * impacto real (proyectado con GetWorldToScreen).
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

    /* Respawn incremental: si count < targetN, cada `interval` nace un
     * sistema nuevo, para que la poblacion nunca llegue a cero. */
    float respawnTimer;

    /* --- explosiones: SoA + tabla de particulas aplanada, mismo patron que
     * ringFirst/planetCount sobre la tabla de anillos (spawn.h) pero con
     * rango fijo (DS_EXP_PARTICLES por explosion, no hace falta ringFirst). */
    int   expCount;
    float expX[DS_MAX_EXPLOSIONS], expY[DS_MAX_EXPLOSIONS];
    float expAge[DS_MAX_EXPLOSIONS], expDur[DS_MAX_EXPLOSIONS];
    float partDir[DS_MAX_EXPLOSIONS * DS_EXP_PARTICLES];
    float partSpd[DS_MAX_EXPLOSIONS * DS_EXP_PARTICLES];
} DeathStar;

/* Construye modelos y texturas. Llamar tras InitWindow (GPU ya lista). */
void deathstar_load(DeathStar *ds, Rng *rng);
void deathstar_unload(DeathStar *ds); /* antes de CloseWindow */

/* Reinicia la maquina de disparo y las explosiones activas, sin recrear
 * modelos. Se llama junto con build_scene (tecla R, cambio de resolucion). */
void deathstar_reset(DeathStar *ds);

/* Avanza rotacion (el ojo gira pegado al cuerpo), dispara cuando el ojo pasa
 * por el frente (con resolucion de impacto: destruye el sistema alcanzado),
 * explosiones y respawn incremental hasta targetN. */
void deathstar_update(DeathStar *ds, World *w, SolarSystems *ss, TrailBuffer *tb,
                      Rng *rng, int targetN, float screenW, float screenH, float dt);

/* Dibuja la estacion (3D), el rayo y las explosiones (2D, proyectados con
 * GetWorldToScreen). Debe correr entre BeginDrawing/EndDrawing, despues de
 * sys_render: la estacion tapa a los sistemas que le pasan por detras. */
void deathstar_render(const DeathStar *ds, int screenW, int screenH);

#endif /* DEATHSTAR_H */
