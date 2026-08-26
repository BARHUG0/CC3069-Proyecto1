/* systems.h - Sistemas del ECS.
 *
 * Un sistema es una funcion libre que recorre los arreglos del World de forma
 * lineal. No hay metodos ni despacho dinamico: el "comportamiento" vive en
 * estos bucles y los datos viven en World.
 *
 * Orden obligatorio por fotograma:
 *      sys_spawn_stars -> sys_drift -> sys_twinkle -> sys_orbit ->
 *      sys_trails -> sys_lifetime -> sys_render
 *
 * La dependencia real es twinkle antes de lifetime: twinkle ESCRIBE alpha con
 * el brillo del centelleo y lifetime lo MULTIPLICA por el sobre de aparicion y
 * desaparicion. Invertirlos haria que las estrellas nunca se desvanezcan.
 *
 * sys_drift va antes de sys_orbit: mueve el centro de cada sistema (ocx/ocy
 * de sus planetas, px/py de su sol) para que sys_orbit reproyecte sobre el
 * centro ya actualizado del fotograma. sys_trails va despues de sys_orbit:
 * muestrea las posiciones ya definitivas del fotograma.
 *
 * sys_drift usa el mismo modelo que sys_orbit (angulo + radio + velocidad
 * angular alrededor de un centro fijo), aplicado un nivel arriba: en vez del
 * centro de un planeta orbitando su sol, es el centro de un sistema entero
 * orbitando una de las dos anclas compartidas en ss->anchorX/Y (que ancla le
 * toca a cada sistema se decide al azar en el spawn, ss->anchor[s], ver
 * spawn.h, independiente de donde nacio el sistema). El radio de orbita sale
 * de la geometria real hacia esa ancla, asi que puede ser grande y el
 * sistema puede pasar temporadas fuera de pantalla; no hay guard de bordes
 * porque es un caso aceptado, no un bug.
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

/* Hace girar cada sistema solar alrededor de su anclaje (ss->anchorX/Y[a]) y
 * propaga el nuevo centro a px/py del sol, ocx/ocy de sus planetas y los
 * centros de anillo: todo lo que depende del centro del sistema queda al dia
 * antes de que sys_orbit reproyecte. */
void sys_drift(World *w, SolarSystems *ss, float dt);

/* --- estelas -------------------------------------------------------------
 * Estado global (como StarField): no es un dato por entidad, asi que vive
 * fuera del World. Guarda un historial circular de posiciones por cuerpo
 * (soles + planetas) muestreado a TRAIL_HZ fijo, para que la estela dure lo
 * mismo sin importar el framerate.
 *
 * Layout [muestra][cuerpo]: el bucle caliente (sys_trails) escribe una
 * rebanada -todos los cuerpos- por tick, asi que ese es el eje contiguo. El
 * color se copia por valor una vez en trails_init (ver ecs.h: copiar en vez
 * de saltar a leer otro arreglo dentro del bucle).
 */
#define TRAIL_LEN        120    /* muestras por cuerpo (~5 s a TRAIL_HZ)   */
#define TRAIL_HZ         24.0f  /* muestreo fijo, independiente del FPS    */
#define MAX_TRAIL_BODIES (MAX_SYSTEMS + MAX_PLANETS_TOTAL)

typedef struct TrailBuffer {
    int     bodyCount;
    Entity  body[MAX_TRAIL_BODIES];
    uint8_t cr[MAX_TRAIL_BODIES];
    uint8_t cg[MAX_TRAIL_BODIES];
    uint8_t cb[MAX_TRAIL_BODIES];

    float x[TRAIL_LEN][MAX_TRAIL_BODIES];
    float y[TRAIL_LEN][MAX_TRAIL_BODIES];

    int   head;  /* proxima rebanada a escribir                */
    int   fill;  /* rebanadas validas, satura en TRAIL_LEN      */
    float accum; /* fraccion de tick de muestreo pendiente      */
} TrailBuffer;

/* Recorre sun[] y ringEntity[] de ss una vez y arma la tabla de cuerpos. */
void trails_init(TrailBuffer *tb, const World *w, const SolarSystems *ss);

/* Empuja una rebanada de posiciones cuando toca (a TRAIL_HZ). */
void sys_trails(const World *w, TrailBuffer *tb, float dt);

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
void sys_render(const World *w, const SolarSystems *ss, const TrailBuffer *tb,
                int showRings, int showTrails);

#endif /* SYSTEMS_H */
