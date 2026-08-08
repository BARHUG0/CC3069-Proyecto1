#ifndef ECS_H
#define ECS_H

#include <stdint.h>

#define ECS_MAX_ENTITIES 262144u

typedef uint32_t Entity;
#define ECS_INVALID ((Entity)0xFFFFFFFFu)

enum ComponentBits {
    C_POS     = 1u << 0,
    C_ORBIT   = 1u << 1,
    C_RENDER  = 1u << 2,
    C_TWINKLE = 1u << 3,
    C_LIFE    = 1u << 4,
    C_SUN     = 1u << 5
};

typedef struct World {
    uint32_t mask[ECS_MAX_ENTITIES];

    float px[ECS_MAX_ENTITIES];
    float py[ECS_MAX_ENTITIES];

    float ocx[ECS_MAX_ENTITIES];
    float ocy[ECS_MAX_ENTITIES];
    float orx[ECS_MAX_ENTITIES];
    float ory[ECS_MAX_ENTITIES];
    float oang[ECS_MAX_ENTITIES];
    float ospd[ECS_MAX_ENTITIES];

    float   rad[ECS_MAX_ENTITIES];
    float   alpha[ECS_MAX_ENTITIES];
    uint8_t cr[ECS_MAX_ENTITIES];
    uint8_t cg[ECS_MAX_ENTITIES];
    uint8_t cb[ECS_MAX_ENTITIES];

    float twPhase[ECS_MAX_ENTITIES];
    float twFreq[ECS_MAX_ENTITIES];
    float twBase[ECS_MAX_ENTITIES];
    float twAmp[ECS_MAX_ENTITIES];

    float life[ECS_MAX_ENTITIES];
    float lifeMax[ECS_MAX_ENTITIES];

    uint32_t capacity;
    uint32_t highWater;
    uint32_t alive;

    Entity   freeList[ECS_MAX_ENTITIES];
    uint32_t freeCount;
} World;

World *ecs_world_alloc(void);
void   ecs_world_free(World *w);
void ecs_reset(World *w);

#endif /* ECS_H */
