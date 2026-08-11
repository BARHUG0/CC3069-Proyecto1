/* systems.h - Sistemas del ECS.
 *
 * Un sistema es una funcion libre que recorre los arreglos del World de forma
 * lineal. No hay metodos ni despacho dinamico: el "comportamiento" vive en
 * estos bucles y los datos viven en World.
 *
 * Orden obligatorio por fotograma:
 *      sys_spawn_stars -> sys_twinkle -> sys_orbit -> sys_lifetime -> sys_render
 *
 * La dependencia real es twinkle antes de lifetime: twinkle ESCRIBE alpha con
 * el brillo del centelleo y lifetime lo MULTIPLICA por el sobre de aparicion y
 * desaparicion. Invertirlos haria que las estrellas nunca se desvanezcan.
 */
#ifndef SYSTEMS_H
#define SYSTEMS_H

#include "ecs.h"
#include "rng.h"
#include "spawn.h"

/* Estado del campo de estrellas. Vive fuera del World porque es global al
 * sistema, no por entidad. */
typedef struct StarField {
    float accumulator; /* fraccion de estrella pendiente de crear */
    float spawnRate;   /* estrellas por segundo                    */
    int   targetStars; /* poblacion de equilibrio                  */
    int   liveStars;   /* estrellas vivas ahora                    */
    float screenW;
    float screenH;
} StarField;

void starfield_init(StarField *sf, int targetStars, float screenW, float screenH);

/* Crea estrellas hasta acercarse a targetStars. Devuelve cuantas creo. */
int sys_spawn_stars(World *w, StarField *sf, Rng *rng, float dt);

/* alpha = twBase + twAmp * sin(twFreq * t + twPhase). Solo lectura/escritura
 * por entidad, sin variables compartidas: paralelizable tal cual. */
void sys_twinkle(World *w, float t);

/* Integra el angulo orbital y proyecta la posicion sobre la elipse.
 * Tambien es puramente por entidad. */
void sys_orbit(World *w, float dt);

/* Descuenta vida, aplica el sobre de fundido y destruye lo agotado.
 * Devuelve cuantas entidades destruyo (para actualizar liveStars). */
int sys_lifetime(World *w, float dt);

/* Dibuja todo. Debe correr entre BeginDrawing/EndDrawing y solo en el hilo
 * principal: OpenGL no es reentrante. */
void sys_render(const World *w, const SolarSystems *ss, int showRings);

#endif /* SYSTEMS_H */
