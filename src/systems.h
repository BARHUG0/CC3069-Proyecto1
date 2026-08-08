#ifndef SYSTEMS_H
#define SYSTEMS_H

#include "ecs.h"
#include "spawn.h"

void sys_twinkle(World *w, float t);
int sys_lifetime(World *w, float dt);
void sys_render(const World *w, const SolarSystems *ss, int showRings);

#endif /* SYSTEMS_H */
