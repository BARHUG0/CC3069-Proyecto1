/* ecs.h - ECS minimo con diseno orientado a datos (DOD).
 *
 * Idea central: NO existe un "struct Estrella" ni un "struct Planeta" con sus
 * metodos. Una entidad es solamente un indice entero. Todos los atributos viven
 * en arreglos paralelos (struct of arrays, SoA), un arreglo por campo:
 *
 *      indice:      0      1      2      3
 *      mask:      POS|R  POS|R  POS|R  POS|R
 *      px:        12.0   80.5   33.1   ...
 *      py:         4.0   19.2   77.0   ...
 *
 * Ventajas frente al diseno orientado a objetos (array of structs):
 *   - Un sistema que solo necesita px/py recorre memoria contigua: cada linea
 *     de cache trae 16 posiciones utiles en vez de 1 objeto con campos que no
 *     se usan.
 *   - No hay punteros ni herencia, asi que no hay saltos indirectos ni vtables.
 *   - Los bucles quedan como "for i en rango: aritmetica pura", que es la forma
 *     que el compilador vectoriza y que despues se puede repartir entre hilos.
 *
 * "Componente" aqui es solo un bit en mask[] mas los arreglos asociados.
 */
#ifndef ECS_H
#define ECS_H

#include <stdint.h>

/* Cota superior de entidades vivas simultaneamente. El mundo completo se
 * reserva de golpe en el heap (~20 MB) para que los indices nunca se
 * invaliden y para no llamar a malloc durante el bucle principal. */
#define ECS_MAX_ENTITIES 262144u

typedef uint32_t Entity;
#define ECS_INVALID ((Entity)0xFFFFFFFFu)

/* Banderas de componente. mask[e] describe que arreglos son validos para e. */
enum ComponentBits {
    C_POS     = 1u << 0, /* px, py                                    */
    C_ORBIT   = 1u << 1, /* ocx, ocy, orx, ory, oang, ospd            */
    C_RENDER  = 1u << 2, /* rad, alpha, cr, cg, cb                    */
    C_TWINKLE = 1u << 3, /* twPhase, twFreq, twBase, twAmp            */
    C_LIFE    = 1u << 4, /* life, lifeMax (entidad efimera)           */
    C_SUN     = 1u << 5  /* marca: se dibuja como estrella central    */
};

typedef struct World {
    /* --- identidad ------------------------------------------------------ */
    uint32_t mask[ECS_MAX_ENTITIES];

    /* --- C_POS: posicion en pixeles de pantalla -------------------------- */
    float px[ECS_MAX_ENTITIES];
    float py[ECS_MAX_ENTITIES];

    /* --- C_ORBIT: elipse alrededor de un centro fijo --------------------
     * El centro se guarda por valor (ocx, ocy) y no como "id del padre".
     * Guardar el id obligaria a leer px[padre] dentro del bucle, es decir un
     * acceso aleatorio a otro arreglo; con el centro copiado el sistema de
     * orbitas queda con acceso puramente secuencial. */
    float ocx[ECS_MAX_ENTITIES];
    float ocy[ECS_MAX_ENTITIES];
    float orx[ECS_MAX_ENTITIES];  /* semieje horizontal */
    float ory[ECS_MAX_ENTITIES];  /* semieje vertical   */
    float oang[ECS_MAX_ENTITIES]; /* angulo actual (rad) */
    float ospd[ECS_MAX_ENTITIES]; /* velocidad angular (rad/s), signo = sentido */

    /* --- C_RENDER: como se pinta ---------------------------------------- */
    float   rad[ECS_MAX_ENTITIES];
    float   alpha[ECS_MAX_ENTITIES]; /* 0..1, resultado de brillo * desvanecido */
    uint8_t cr[ECS_MAX_ENTITIES];
    uint8_t cg[ECS_MAX_ENTITIES];
    uint8_t cb[ECS_MAX_ENTITIES];

    /* --- C_TWINKLE: brillo senoidal ------------------------------------- */
    float twPhase[ECS_MAX_ENTITIES];
    float twFreq[ECS_MAX_ENTITIES];
    float twBase[ECS_MAX_ENTITIES];
    float twAmp[ECS_MAX_ENTITIES];

    /* --- C_LIFE: tiempo de vida restante -------------------------------- */
    float life[ECS_MAX_ENTITIES];
    float lifeMax[ECS_MAX_ENTITIES];

    /* --- administracion de indices -------------------------------------- */
    uint32_t capacity;  /* == ECS_MAX_ENTITIES */
    uint32_t highWater; /* indices 0..highWater-1 fueron usados alguna vez */
    uint32_t alive;     /* entidades vivas ahora */

    /* Pila de indices reciclados: crear/destruir es O(1) y sin huecos que
     * crezcan sin control. Es la unica estructura compartida con escritura,
     * o sea el punto a proteger cuando se paralelice (ver PARALELIZACION.md). */
    Entity   freeList[ECS_MAX_ENTITIES];
    uint32_t freeCount;
} World;

World *ecs_world_alloc(void);
void   ecs_world_free(World *w);

/* Borra todas las entidades sin liberar memoria. */
void ecs_reset(World *w);

/* Devuelve un indice con mask = 0 y campos en cero, o ECS_INVALID si se lleno. */
Entity ecs_create(World *w);

/* Marca el indice como libre y lo devuelve a la pila de reciclaje. */
void ecs_destroy(World *w, Entity e);

/* Prueba de pertenencia: e tiene TODOS los bits de bits. */
static inline int ecs_has(const World *w, Entity e, uint32_t bits)
{
    return (w->mask[e] & bits) == bits;
}

#endif /* ECS_H */
