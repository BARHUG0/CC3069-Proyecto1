/* deathstar.h - Modalidad opcional --deadstar: una Estrella de la Muerte 2D
 * en el centro de la pantalla que dispara el superlaser cada cierto tiempo a
 * un punto al azar; si le pega a un sistema solar, lo destruye.
 *
 * Dibujada en 2D con las mismas primitivas que el resto de sys_render (nada
 * de Camera3D/Model): un circulo con paneles y trinchera, un "ojo" (el
 * superlaser) chico y SIEMPRE en el centro geometrico del circulo, y luces
 * amarillas repartidas por todo el disco. El ojo no rota para "apuntar": el
 * rayo mismo va del centro al objetivo, asi que no hay desalineacion entre
 * hacia donde mira el ojo y hacia donde pega el disparo.
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

#define DS_MAX_LIGHTS      56
#define DS_MAX_EXPLOSIONS   8
#define DS_EXP_PARTICLES   24

enum DeathStarPhase {
    DS_IDLE = 0,   /* esperando el proximo disparo               */
    DS_CHARGE,     /* apuntando, los 8 haces del borde se cargan  */
    DS_FIRE        /* rayo visible; el impacto se resuelve al entrar */
};

typedef struct DeathStar {
    /* --- estacion, todo 2D --- */
    float spin;      /* grados: gira el patron de paneles/luces, cosmetico */
    float bodyFrac;  /* radio del cuerpo = bodyFrac * screenH/2            */
    float dishFrac;  /* radio del ojo = dishFrac * radio del cuerpo        */

    /* Luces amarillas: offsets fijos (generados una vez en deathstar_load)
     * dentro del circulo unidad, para que no salten de frame a frame; se
     * escalan por el radio del cuerpo y se rotan por spin al dibujar. */
    int   lightCount;
    float lightDX[DS_MAX_LIGHTS];
    float lightDY[DS_MAX_LIGHTS];
    float lightR[DS_MAX_LIGHTS];

    /* --- maquina de disparo --- */
    int   phase;
    float timer;     /* cuenta atras de la fase actual (s)         */
    float interval;  /* periodo entre disparos, de --deadstar SECS */
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

/* Genera las luces amarillas fijas y arranca la maquina de disparo. No hay
 * recursos de GPU que cargar (todo se dibuja con primitivas 2D), asi que no
 * existe una deathstar_unload. */
void deathstar_load(DeathStar *ds, Rng *rng, float secs);

/* Reinicia la maquina de disparo y las explosiones activas (no las luces,
 * que son fijas). Se llama junto con build_scene (tecla R, cambio de
 * resolucion). */
void deathstar_reset(DeathStar *ds);

/* Avanza rotacion, maquina de disparo (con resolucion de impacto: destruye el
 * sistema alcanzado), explosiones y respawn incremental hasta targetN. */
void deathstar_update(DeathStar *ds, World *w, SolarSystems *ss, TrailBuffer *tb,
                      Rng *rng, int targetN, float screenW, float screenH, float dt);

/* Dibuja la estacion, el rayo y las explosiones, todo en 2D. Debe correr
 * entre BeginDrawing/EndDrawing, despues de sys_render: la estacion tapa a
 * los sistemas que le pasan por detras. */
void deathstar_render(const DeathStar *ds, int screenW, int screenH);

#endif /* DEATHSTAR_H */
