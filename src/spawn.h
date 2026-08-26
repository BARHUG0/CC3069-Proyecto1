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
} SolarSystems;

/* Crea una estrella de fondo efimera (C_POS|C_RENDER|C_TWINKLE|C_LIFE).
 * Devuelve ECS_INVALID si el mundo esta lleno. */
Entity spawn_star(World *w, Rng *rng, float screenW, float screenH);

/* Coloca n sistemas solares en una rejilla con jitter y crea sol + planetas.
 * Sobreescribe por completo el contenido de ss. */
void spawn_solar_systems(World *w, SolarSystems *ss, Rng *rng,
                         int n, float screenW, float screenH);

#endif /* SPAWN_H */
