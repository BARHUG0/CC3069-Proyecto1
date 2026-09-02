/* spawn.h - Fabricas de entidades: estrellas de fondo y sistemas solares.
 *
 * Los "prefabs" no son clases: cada fabrica solo escribe bits en mask[] y
 * valores en los arreglos del World.
 */
#ifndef SPAWN_H
#define SPAWN_H

#include "ecs.h"
#include "rng.h"

/* Tope de cordura para N (numero de sistemas). La memoria real se reserva a la
 * N pedida en tiempo de ejecucion (solar_systems_alloc), no a este maximo. */
#define MAX_SYSTEMS       2000000
#define MIN_PLANETS             2
#define MAX_PLANETS_PER_SYS     8

/* Profundidad: cada sistema nace con su PROPIO valor unico en [0,1] (0 = mas
 * atras/chica/tenue, 1 = mas al frente, tamano/brillo completos) para que la
 * distancia se vea SIEMPRE, no solo cuando dos sistemas se cruzan. A
 * diferencia de las capas fijas de antes, dos sistemas nunca comparten
 * profundidad — ni al nacer (reparto uniforme + barajado, spawn.c) ni tras
 * morir y renacer (punto medio del hueco mas ancho, spawn_one_system).
 * spawn.c hornea la profundidad en la geometria (radio del sol, radios de
 * orbita, velocidad) una sola vez al nacer; systems.c la usa en el render
 * para el orden de pintado (los opacos se ordenan atras->adelante por
 * profundidad, asi el de encima siempre tapa al de atras) y para el brillo
 * (aditivo del resplandor solar, estelas). Estas dos funciones son la unica
 * fuente de verdad para esa escala/brillo — usadas tanto al nacer (spawn.c)
 * como al pintar (systems.c) para que no se desincronicen. */
static inline float solar_depth_scale(float depth)
{
    return 0.45f + 0.55f * depth;
}

static inline float solar_depth_alpha(float depth)
{
    return 0.20f + 0.80f * depth;
}

/* Datos de los sistemas solares, tambien en SoA.
 *
 * Los planetas ya son entidades del World; esta estructura guarda aparte la
 * geometria de las orbitas porque el anillo se dibuja una vez por orbita y no
 * depende del angulo actual del planeta. */
typedef struct SolarSystems {
    /* Arreglos en heap reservados por solar_systems_alloc: los de sistema a
     * maxSystems entradas, los de anillo a maxRing (= maxSystems *
     * MAX_PLANETS_PER_SYS). Punteros y no arreglos fijos para dimensionar a la
     * N pedida (6 .. ~1e6). */
    int    maxSystems;
    int    maxRing;

    int    count;
    float  *cx;
    float  *cy;
    Entity *sun;
    int    *planetCount;

    /* Anillos aplanados: el sistema s ocupa [ringFirst[s], ringFirst[s]+planetCount[s]) */
    int   *ringFirst;
    float *ringCx;
    float *ringCy;
    float *ringRx;
    float *ringRy;
    /* Entidad planeta del anillo i. Junta explicita entre esta tabla y el
     * World: los ids son consecutivos en la practica (ecs_reset justo antes),
     * pero eso es un accidente del orden de creacion, no una garantia. */
    Entity *ringEntity;
    int   ringTotal;

    int totalPlanets;

    /* Scratch para el orden de pintado atras->adelante (systems.c
     * render_bodies), maxSystems ints. Vive aca para no reservarlo por frame. */
    int *renderOrder;

    /* --- deriva: dos anclas fijas, una por mitad de pantalla, con el mismo
     * modelo (angulo + radio + velocidad angular) que ya usan los planetas
     * alrededor de su sol, aplicado un nivel arriba. La posicion de cada
     * sistema se decide primero (rejilla + jitter, independiente del ancla);
     * que ancla le toca se decide aparte con un reparto barajado (mitad y
     * mitad, orden al azar via Fisher-Yates — no un volado independiente por
     * sistema, que con N chico puede caer muy desparejo por puro azar). El
     * radio/angulo de orbita salen de la geometria real entre posicion y
     * ancla sorteada, asi que un sistema nacido en una mitad puede terminar
     * orbitando el ancla de la otra y cruzarla — es el comportamiento pedido,
     * no un defecto. Consecuencia aceptada: un sistema con ancla lejana tiene
     * radio grande y puede salirse de pantalla por momentos. */
    float anchorX[2];
    float anchorY[2];
    int   *anchor;  /* 0 o 1: que ancla le toca, al azar    */
    float *orbRad;  /* radio de orbita alrededor del ancla */
    float *orbAng;  /* angulo actual (rad)                 */
    float *orbSpd;  /* velocidad angular (rad/s), con signo */

    /* Profundidad unica por sistema en [0,1], ver solar_depth_* arriba.
     * IMPORTANTE: como anchor/orbRad/orbAng/orbSpd de arriba, debe copiarse en
     * el swap-remove de solar_system_remove (spawn.c) o queda desactualizada
     * tras un impacto de la Estrella de la Muerte. */
    float *depth;

    /* Layout de rejilla de spawn_solar_systems, guardado para que
     * spawn_one_system pueda hacer nacer un sistema nuevo (p.ej. tras un
     * impacto de la Estrella de la Muerte) con la misma geometria que los del
     * arranque, sin recalcular ni duplicar constantes. */
    int   gridCols, gridRows;
    float cellW, cellH, cellR;
    float sunRad, planetRef, jitter;
} SolarSystems;

/* Reserva SolarSystems para maxSystems sistemas (y maxSystems*MAX_PLANETS_PER_SYS
 * slots de anillo). Devuelve NULL si algun arreglo no cupo en memoria. */
SolarSystems *solar_systems_alloc(int maxSystems);
void solar_systems_free(SolarSystems *ss);
/* Vacia los contadores (count/ringTotal/totalPlanets) sin liberar memoria;
 * el resto lo sobreescribe spawn_solar_systems. */
void solar_systems_reset(SolarSystems *ss);

/* Crea una estrella de fondo efimera (C_POS|C_RENDER|C_TWINKLE|C_LIFE).
 * Devuelve ECS_INVALID si el mundo esta lleno. */
Entity spawn_star(World *w, Rng *rng, float screenW, float screenH);

/* Coloca n sistemas solares en una rejilla con jitter y crea sol + planetas.
 * Sobreescribe por completo el contenido de ss. */
void spawn_solar_systems(World *w, SolarSystems *ss, Rng *rng,
                         int n, float screenW, float screenH);

/* Hace nacer un sistema mas al final de ss (ss->count++), reusando el layout
 * de rejilla guardado por spawn_solar_systems. Celda al azar (solaparse con
 * un sistema vivo es aceptado, igual que en el spawn inicial) y ancla = la
 * que tenga menos sistemas ahora mismo (empate = volado), para no romper el
 * balance 50/50 sin volver a un volado independiente por sistema. Devuelve el
 * indice del sistema nuevo, o -1 si ss o el World ya estan llenos. */
int spawn_one_system(World *w, SolarSystems *ss, Rng *rng);

/* Destruye el sol y los planetas del sistema s (ecs_destroy), compacta la
 * tabla de anillos aplanada y hace swap-remove del slot s con el ultimo
 * sistema. Tras esto los indices de sistema >= s pueden apuntar a otro
 * sistema: el llamador no puede asumir que el orden se mantiene. */
void solar_system_remove(World *w, SolarSystems *ss, int s);

#endif /* SPAWN_H */
