#include "ecs.h"

#include <stdlib.h>
#include <string.h>

World *ecs_world_alloc(void)
{
    /* calloc y no malloc: deja todos los arreglos (incluido mask) en cero,
     * que es exactamente el estado "mundo vacio". */
    World *w = (World *)calloc(1, sizeof(World));
    if (w == NULL) {
        return NULL;
    }
    w->capacity = ECS_MAX_ENTITIES;
    return w;
}

void ecs_world_free(World *w)
{
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
