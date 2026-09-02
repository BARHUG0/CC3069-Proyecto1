#include "ecs.h"

#include <stdlib.h>
#include <string.h>

/* Cada arreglo SoA se reserva por separado con calloc (deja todo en cero =
 * "mundo vacio"). ALLOC deja `w` medio construido si algo falla; el llamador
 * hace ecs_world_free, que tolera punteros NULL. */
#define ECS_ALLOC_FIELD(field, count)                       \
    do {                                                    \
        w->field = calloc((count), sizeof(*w->field));      \
        if (w->field == NULL) { ecs_world_free(w); return NULL; } \
    } while (0)

World *ecs_world_alloc(uint32_t capacity)
{
    if (capacity == 0u) {
        capacity = ECS_MAX_ENTITIES;
    }

    World *w = (World *)calloc(1, sizeof(World));
    if (w == NULL) {
        return NULL;
    }
    w->capacity = capacity;

    ECS_ALLOC_FIELD(mask,     capacity);
    ECS_ALLOC_FIELD(px,       capacity);
    ECS_ALLOC_FIELD(py,       capacity);
    ECS_ALLOC_FIELD(ocx,      capacity);
    ECS_ALLOC_FIELD(ocy,      capacity);
    ECS_ALLOC_FIELD(orx,      capacity);
    ECS_ALLOC_FIELD(ory,      capacity);
    ECS_ALLOC_FIELD(oang,     capacity);
    ECS_ALLOC_FIELD(ospd,     capacity);
    ECS_ALLOC_FIELD(rad,      capacity);
    ECS_ALLOC_FIELD(alpha,    capacity);
    ECS_ALLOC_FIELD(cr,       capacity);
    ECS_ALLOC_FIELD(cg,       capacity);
    ECS_ALLOC_FIELD(cb,       capacity);
    ECS_ALLOC_FIELD(twPhase,  capacity);
    ECS_ALLOC_FIELD(twFreq,   capacity);
    ECS_ALLOC_FIELD(twBase,   capacity);
    ECS_ALLOC_FIELD(twAmp,    capacity);
    ECS_ALLOC_FIELD(life,     capacity);
    ECS_ALLOC_FIELD(lifeMax,  capacity);
    ECS_ALLOC_FIELD(freeList, capacity);

    return w;
}

#undef ECS_ALLOC_FIELD

void ecs_world_free(World *w)
{
    if (w == NULL) {
        return;
    }
    free(w->mask);
    free(w->px);      free(w->py);
    free(w->ocx);     free(w->ocy);
    free(w->orx);     free(w->ory);
    free(w->oang);    free(w->ospd);
    free(w->rad);     free(w->alpha);
    free(w->cr);      free(w->cg);      free(w->cb);
    free(w->twPhase); free(w->twFreq);  free(w->twBase);  free(w->twAmp);
    free(w->life);    free(w->lifeMax);
    free(w->freeList);
    free(w);
}

void ecs_reset(World *w)
{
    /* Solo hace falta limpiar mask: los demas arreglos se sobreescriben al
     * crear la entidad, y sin bit de componente nadie los lee. */
    memset(w->mask, 0, sizeof(uint32_t) * w->highWater);
    w->highWater = 0;
    w->alive     = 0;
    w->freeCount = 0;
}

Entity ecs_create(World *w)
{
    Entity e;

    if (w->freeCount > 0u) {
        e = w->freeList[--w->freeCount];
    } else if (w->highWater < w->capacity) {
        e = w->highWater++;
    } else {
        return ECS_INVALID;
    }

    /* Un indice reciclado trae basura del inquilino anterior: se normaliza
     * para que un componente agregado a medias nunca lea valores viejos. */
    w->mask[e]    = 0u;
    w->px[e]      = 0.0f;
    w->py[e]      = 0.0f;
    w->ocx[e]     = 0.0f;
    w->ocy[e]     = 0.0f;
    w->orx[e]     = 0.0f;
    w->ory[e]     = 0.0f;
    w->oang[e]    = 0.0f;
    w->ospd[e]    = 0.0f;
    w->rad[e]     = 0.0f;
    w->alpha[e]   = 0.0f;
    w->cr[e]      = 0u;
    w->cg[e]      = 0u;
    w->cb[e]      = 0u;
    w->twPhase[e] = 0.0f;
    w->twFreq[e]  = 0.0f;
    w->twBase[e]  = 0.0f;
    w->twAmp[e]   = 0.0f;
    w->life[e]    = 0.0f;
    w->lifeMax[e] = 0.0f;

    w->alive++;
    return e;
}

void ecs_destroy(World *w, Entity e)
{
    if (e >= w->highWater || w->mask[e] == 0u) {
        return; /* ya estaba libre: evita meter el mismo indice dos veces */
    }
    w->mask[e] = 0u;
    w->freeList[w->freeCount++] = e;
    w->alive--;
}
