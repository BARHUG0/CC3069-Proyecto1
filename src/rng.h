/* rng.h - Generador xorshift32 en linea.
 *
 * Se usa en lugar de rand() por tres razones:
 *   1. Es reproducible: la misma semilla da exactamente la misma escena.
 *   2. No usa estado global oculto, cada llamada recibe su propio Rng.
 *      Eso permite, mas adelante, dar un Rng por hilo sin condiciones de carrera.
 *   3. Es unas 10x mas rapido que rand() y no toca la libreria estandar.
 */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct Rng {
    uint32_t state;
} Rng;

static inline void rng_seed(Rng *r, uint32_t seed)
{
    /* El estado cero es un punto fijo de xorshift: siempre devolveria 0. */
    r->state = (seed != 0u) ? seed : 0x9E3779B9u;
}

static inline uint32_t rng_u32(Rng *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

/* Flotante en [0, 1). Se usan 24 bits para no perder precision en float. */
static inline float rng_f01(Rng *r)
{
    return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

static inline float rng_range(Rng *r, float lo, float hi)
{
    return lo + (hi - lo) * rng_f01(r);
}

static inline uint32_t rng_below(Rng *r, uint32_t n)
{
    return (n != 0u) ? (rng_u32(r) % n) : 0u;
}

static inline float rng_sign(Rng *r)
{
    return (rng_u32(r) & 1u) ? 1.0f : -1.0f;
}

#endif /* RNG_H */
