/* spawn.h - Fabricas de entidades: estrellas de fondo y sistemas solares.
 *
 * Los "prefabs" no son clases: cada fabrica solo escribe bits en mask[] y
 * valores en los arreglos del World.
 */
#ifndef SPAWN_H
#define SPAWN_H

#include "ecs.h"
#include "rng.h"

#define MAX_SYSTEMS         256
#define MIN_PLANETS           2
#define MAX_PLANETS_PER_SYS   8
#define MAX_PLANETS_TOTAL   (MAX_SYSTEMS * MAX_PLANETS_PER_SYS)

/* Capas de profundidad: cada sistema nace en una de SYS_LAYER_COUNT capas
 * fijas (0 = mas atras/chica/tenue, SYS_LAYER_COUNT-1 = mas al frente,
 * tamano/brillo completos) para que la distancia se vea SIEMPRE, no solo
 * cuando dos sistemas se cruzan. spawn.c hornea la capa en la geometria
 * (radio del sol, radios de orbita, velocidad) una sola vez al nacer;
 * systems.c la usa en el render para el orden de pintado (opacos: el sol y
 * los planetas de una capa se pintan todos antes que la siguiente, asi el
 * de encima siempre tapa al de atras) y para el brillo (aditivo del
 * resplandor solar, estelas). Estas dos funciones son la unica fuente de
 * verdad para esa escala/brillo — usadas tanto al nacer (spawn.c) como al
 * pintar (systems.c) para que no se desincronicen. */
#define SYS_LAYER_COUNT 4

static inline float solar_layer_scale(int layer)
{
    if (SYS_LAYER_COUNT <= 1) return 1.0f;
    const float t = (float)layer / (float)(SYS_LAYER_COUNT - 1);
    return 0.60f + 0.40f * t;
}

static inline float solar_layer_alpha(int layer)
{
    if (SYS_LAYER_COUNT <= 1) return 1.0f;
    const float t = (float)layer / (float)(SYS_LAYER_COUNT - 1);
    return 0.50f + 0.50f * t;
}

/* Datos de los sistemas solares, tambien en SoA.
 *
 * Los planetas ya son entidades del World; esta estructura guarda aparte la
 * geometria de las orbitas porque el anillo se dibuja una vez por orbita y no
 * depende del angulo actual del planeta. */
typedef struct SolarSystems {
    int    count;
    float  cx[MAX_SYSTEMS];
    float  cy[MAX_SYSTEMS];
    Entity sun[MAX_SYSTEMS];
    int    planetCount[MAX_SYSTEMS];

    /* Anillos aplanados: el sistema s ocupa [ringFirst[s], ringFirst[s]+planetCount[s]) */
    int   ringFirst[MAX_SYSTEMS];
    float ringCx[MAX_PLANETS_TOTAL];
    float ringCy[MAX_PLANETS_TOTAL];
    float ringRx[MAX_PLANETS_TOTAL];
    float ringRy[MAX_PLANETS_TOTAL];
    /* Entidad planeta del anillo i. Junta explicita entre esta tabla y el
     * World: los ids son consecutivos en la practica (ecs_reset justo antes),
     * pero eso es un accidente del orden de creacion, no una garantia. */
    Entity ringEntity[MAX_PLANETS_TOTAL];
    int   ringTotal;

    int totalPlanets;

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
    int   anchor[MAX_SYSTEMS];  /* 0 o 1: que ancla le toca, al azar    */
    float orbRad[MAX_SYSTEMS];  /* radio de orbita alrededor del ancla */
    float orbAng[MAX_SYSTEMS];  /* angulo actual (rad)                 */
    float orbSpd[MAX_SYSTEMS];  /* velocidad angular (rad/s), con signo */

    /* Capa de profundidad, ver SYS_LAYER_COUNT arriba. IMPORTANTE: como
     * anchor/orbRad/orbAng/orbSpd de arriba, debe copiarse en el
     * swap-remove de solar_system_remove (spawn.c) o queda desactualizada
     * tras un impacto de la Estrella de la Muerte. */
    int layer[MAX_SYSTEMS];

    /* Layout de rejilla de spawn_solar_systems, guardado para que
     * spawn_one_system pueda hacer nacer un sistema nuevo (p.ej. tras un
     * impacto de la Estrella de la Muerte) con la misma geometria que los del
     * arranque, sin recalcular ni duplicar constantes. */
    int   gridCols, gridRows;
    float cellW, cellH, cellR;
    float sunRad, planetRef, jitter;
} SolarSystems;

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
