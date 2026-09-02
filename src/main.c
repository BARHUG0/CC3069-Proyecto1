/* main.c - Screensaver: campo de estrellas + N sistemas solares.
 *
 * Uso:  screensaver N [opciones]
 *
 * Arquitectura: ECS orientado a datos (ver ecs.h). main solo se encarga de
 * argumentos, ventana, entrada de teclado y de llamar a los sistemas en orden.
 * Ninguna entidad tiene metodos ni se actualiza a si misma.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raylib.h"

#include "deathstar.h"
#include "ecs.h"
#include "rng.h"
#include "spawn.h"
#include "systems.h"

#ifdef PARALLEL_SYSTEMS
#include <omp.h>
#define SYSTEMS_MODE "parallel"
#define SYSTEMS_THREADS() omp_get_max_threads()
#define active_sys_spawn_stars sys_spawn_stars_parallel
#define active_sys_drift sys_drift_parallel
#define active_sys_twinkle sys_twinkle_parallel
#define active_sys_orbit sys_orbit_parallel
#define active_sys_trails sys_trails_parallel
#define active_sys_lifetime sys_lifetime_parallel
#define active_sys_render sys_render_parallel
#else
#define SYSTEMS_MODE "sequential"
#define SYSTEMS_THREADS() 1
#define active_sys_spawn_stars sys_spawn_stars
#define active_sys_drift sys_drift
#define active_sys_twinkle sys_twinkle
#define active_sys_orbit sys_orbit
#define active_sys_trails sys_trails
#define active_sys_lifetime sys_lifetime
#define active_sys_render sys_render
#endif

#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT  720
#define DEFAULT_STARS  1200
#define BENCHMARK_WARMUP_SECS 3.0
#define BENCHMARK_DURATION_SECS 10.0
#define BENCHMARK_SAMPLES 10

/* Presupuesto de estrellas: lo que queda del mundo tras reservar sitio para
 * todos los soles y planetas posibles. */
#define MAX_STARS (ECS_MAX_ENTITIES - MAX_PLANETS_TOTAL - MAX_SYSTEMS - 1024u)

typedef struct Config {
    int          systems;    /* N, obligatorio                     */
    int          stars;      /* poblacion objetivo del campo       */
    int          width;
    int          height;
    unsigned int seed;       /* 0 = derivar del reloj              */
    int          fullscreen;
    long         frames;     /* 0 = infinito (modo screensaver)    */
    const char  *screenshot; /* NULL = no capturar                 */
    int          rings;      /* dibujar anillos de orbita          */
    int          trails;     /* dibujar estelas de sol y planetas  */
    int          vsync;      /* 0 = sin limite, para medir         */
    int          hud;        /* mostrar el panel al arrancar       */
    int          deadstar;   /* modalidad Estrella de la Muerte    */
    int          deadstarStatic;
    int          targetFps;
    int          benchmark;
} Config;

static void print_usage(const char *exe)
{
    printf("Uso: %s N [opciones]\n\n", exe);
    printf("  N                  numero de sistemas solares (1-%d)\n", MAX_SYSTEMS);
    printf("\nOpciones:\n");
    printf("  --stars M          estrellas de fondo simultaneas (default %d, max %u)\n",
           DEFAULT_STARS, MAX_STARS);
    printf("  --width W          ancho de ventana (default %d)\n", DEFAULT_WIDTH);
    printf("  --height H         alto de ventana (default %d)\n", DEFAULT_HEIGHT);
    printf("  --seed S           semilla del generador (default: reloj)\n");
    printf("  --fullscreen       arrancar en pantalla completa sin borde\n");
    printf("  --no-rings         no dibujar los anillos de las orbitas\n");
    printf("  --no-trails        no dibujar las estelas de sol y planetas\n");
    printf("  --no-vsync         sin sincronia vertical (para medir FPS reales)\n");
    printf("  --hud              arrancar con el panel de datos visible\n");
    printf("  --deadstar         Estrella de la Muerte controlable con WASD y Space\n");
    printf("  --deadstar-static  mostrar la Estrella de la Muerte sin animarla\n");
    printf("  --target-fps F     limitar la ejecucion a F FPS con raylib\n");
    printf("  --benchmark        medir FPS durante 10 s tras 3 s de calentamiento\n");
    printf("  --frames K         salir tras K fotogramas (para pruebas)\n");
    printf("  --screenshot RUTA  guardar un PNG y seguir (para pruebas)\n");
    printf("  -h, --help         esta ayuda\n");
    printf("\nTeclas: WASD mover | SPACE disparar | P pausa | H hud | O orbitas | T estelas | R nueva escena | F pantalla | ESC salir\n");
}

/* Lee un entero de argv[i+1]. Devuelve 0 si falta o no es valido. */
static int parse_long(int argc, char **argv, int *i, const char *flag, long *out)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "Error: %s requiere un valor.\n", flag);
        return 0;
    }
    char *end = NULL;
    const long v = strtol(argv[*i + 1], &end, 10);
    if (end == argv[*i + 1] || *end != '\0') {
        fprintf(stderr, "Error: %s espera un entero, se recibio '%s'.\n", flag, argv[*i + 1]);
        return 0;
    }
    *out = v;
    *i += 1;
    return 1;
}

/* 0 = ok, 1 = error, 2 = mostrar ayuda y salir. */
static int parse_args(int argc, char **argv, Config *cfg)
{
    cfg->systems    = -1;
    cfg->stars      = DEFAULT_STARS;
    cfg->width      = DEFAULT_WIDTH;
    cfg->height     = DEFAULT_HEIGHT;
    cfg->seed       = 0u;
    cfg->fullscreen = 0;
    cfg->frames     = 0;
    cfg->screenshot = NULL;
    cfg->rings      = 1;
    cfg->trails     = 1;
    cfg->vsync      = 1;
    cfg->hud        = 0;
    cfg->deadstar   = 0;
    cfg->deadstarStatic = 0;
    cfg->targetFps  = 0;
    cfg->benchmark  = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        long v = 0;

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            return 2;
        } else if (strcmp(a, "--stars") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->stars = (int)v;
        } else if (strcmp(a, "--width") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->width = (int)v;
        } else if (strcmp(a, "--height") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->height = (int)v;
        } else if (strcmp(a, "--seed") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->seed = (unsigned int)v;
        } else if (strcmp(a, "--frames") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->frames = v;
        } else if (strcmp(a, "--fullscreen") == 0) {
            cfg->fullscreen = 1;
        } else if (strcmp(a, "--no-rings") == 0) {
            cfg->rings = 0;
        } else if (strcmp(a, "--no-trails") == 0) {
            cfg->trails = 0;
        } else if (strcmp(a, "--no-vsync") == 0) {
            cfg->vsync = 0;
        } else if (strcmp(a, "--hud") == 0) {
            cfg->hud = 1;
        } else if (strcmp(a, "--deadstar") == 0) {
            cfg->deadstar = 1;
        } else if (strcmp(a, "--deadstar-static") == 0) {
            cfg->deadstar = 1;
            cfg->deadstarStatic = 1;
        } else if (strcmp(a, "--target-fps") == 0) {
            if (!parse_long(argc, argv, &i, a, &v)) return 1;
            cfg->targetFps = (int)v;
        } else if (strcmp(a, "--benchmark") == 0) {
            cfg->benchmark = 1;
        } else if (strcmp(a, "--screenshot") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --screenshot requiere una ruta.\n");
                return 1;
            }
            cfg->screenshot = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "Error: opcion desconocida '%s'.\n", a);
            return 1;
        } else if (cfg->systems < 0) {
            char *end = NULL;
            const long n = strtol(a, &end, 10);
            if (end == a || *end != '\0') {
                fprintf(stderr, "Error: N debe ser un entero, se recibio '%s'.\n", a);
                return 1;
            }
            cfg->systems = (int)n;
        } else {
            fprintf(stderr, "Error: argumento posicional de sobra '%s'.\n", a);
            return 1;
        }
    }

    if (cfg->systems < 0) {
        fprintf(stderr, "Error: falta el argumento N (numero de sistemas solares).\n\n");
        return 1;
    }
    if (cfg->systems < 1 || cfg->systems > MAX_SYSTEMS) {
        fprintf(stderr, "Error: N debe estar entre 1 y %d (se recibio %d).\n",
                MAX_SYSTEMS, cfg->systems);
        return 1;
    }
    if (cfg->stars < 0) {
        fprintf(stderr, "Error: --stars no puede ser negativo.\n");
        return 1;
    }
    if ((unsigned int)cfg->stars > MAX_STARS) {
        fprintf(stderr, "Aviso: --stars %d excede el maximo, se limita a %u.\n",
                cfg->stars, MAX_STARS);
        cfg->stars = (int)MAX_STARS;
    }
    if (cfg->width < 320 || cfg->height < 240) {
        fprintf(stderr, "Error: la ventana minima es 320x240.\n");
        return 1;
    }
    if (cfg->frames < 0) {
        cfg->frames = 0;
    }
    if (cfg->targetFps < 0) {
        fprintf(stderr, "Error: --target-fps debe ser mayor que cero.\n");
        return 1;
    }
    if (cfg->benchmark && cfg->vsync) {
        fprintf(stderr, "Error: --benchmark requiere --no-vsync.\n");
        return 1;
    }
    if (cfg->benchmark && cfg->targetFps > 0) {
        fprintf(stderr, "Error: --benchmark no admite --target-fps.\n");
        return 1;
    }
    return 0;
}

/* Reconstruye la escena completa para una resolucion dada. Se llama al inicio,
 * al cambiar de tamano y al pedir una escena nueva con R. */
static void build_scene(World *w, SolarSystems *ss, StarField *sf, TrailBuffer *tb,
                        Rng *rng, const Config *cfg, int screenW, int screenH)
{
    ecs_reset(w);
    spawn_solar_systems(w, ss, rng, cfg->systems, (float)screenW, (float)screenH);
    starfield_init(w, sf, rng, cfg->stars, (float)screenW, (float)screenH);
    trails_init(tb, w, ss);
}

static void draw_hud(const World *w, const SolarSystems *ss, const StarField *sf,
                     const DeathStar *ds, unsigned int seed, int paused)
{
    const int x = 12, y = 12, pw = 336, ph = (ds != NULL) ? 147 : 132;

    DrawRectangle(x, y, pw, ph, (Color){ 0, 0, 0, 150 });
    DrawRectangleLines(x, y, pw, ph, (Color){ 90, 110, 160, 180 });

    int line = y + 8;
    const int step = 15;

    DrawText(TextFormat("FPS: %d   (%.2f ms/frame)", GetFPS(), GetFrameTime() * 1000.0f),
             x + 10, line, 12, (Color){ 140, 240, 160, 255 });
    line += step;
    DrawText(TextFormat("Sistemas solares (N): %d", ss->count),
             x + 10, line, 12, RAYWHITE);
    line += step;
    DrawText(TextFormat("Planetas en orbita: %d", ss->totalPlanets),
             x + 10, line, 12, RAYWHITE);
    line += step;
    DrawText(TextFormat("Estrellas: %d / %d", sf->liveStars, sf->targetStars),
             x + 10, line, 12, RAYWHITE);
    line += step;
    DrawText(TextFormat("Entidades vivas: %u  (pico %u / %u)",
                        w->alive, w->highWater, w->capacity),
             x + 10, line, 12, (Color){ 190, 200, 220, 255 });
    line += step;
    DrawText(TextFormat("Semilla: %u%s", seed, paused ? "   [PAUSA]" : ""),
             x + 10, line, 12, (Color){ 190, 200, 220, 255 });
    line += step;
    if (ds != NULL) {
        if (ds->phase == DS_IDLE) {
            DrawText(TextFormat("Estrella de la Muerte: lista  Sistemas destruidos: %d", ds->kills),
                     x + 10, line, 12, (Color){ 255, 170, 140, 255 });
        } else {
            const char *phaseName = (ds->phase == DS_CHARGE) ? "cargando" : "FUEGO";
            DrawText(TextFormat("Estrella de la Muerte: %s (%.1fs)  Sistemas destruidos: %d",
                                phaseName, ds->timer > 0.0f ? ds->timer : 0.0f, ds->kills),
                     x + 10, line, 12, (Color){ 255, 170, 140, 255 });
        }
        line += step;
    }
    line += 3;
    DrawText("WASD mover | SPACE disparar | P pausa | H hud | O orbitas | T estelas | R nueva | F pantalla",
             x + 10, line, 10, (Color){ 150, 160, 185, 255 });
}

int main(int argc, char **argv)
{
    Config cfg;
    const int pr = parse_args(argc, argv, &cfg);
    if (pr == 2) {
        print_usage(argv[0]);
        return 0;
    }
    if (pr != 0) {
        print_usage(argv[0]);
        return 1;
    }

    unsigned int seed = cfg.seed;
    if (seed == 0u) {
        seed = (unsigned int)time(NULL);
    }

    World *world = ecs_world_alloc();
    if (world == NULL) {
        fprintf(stderr, "Error: no se pudo reservar el mundo (%.1f MB).\n",
                (double)sizeof(World) / (1024.0 * 1024.0));
        return 1;
    }

    /* SolarSystems y TrailBuffer tambien van al heap: SolarSystems son ~40 KB,
     * TrailBuffer ~1.8 MB (TRAIL_LEN muestras x MAX_TRAIL_BODIES cuerpos x 2
     * ejes), pero mantener el stack limpio evita sorpresas en hilos con pila
     * pequena. */
    SolarSystems *ss = (SolarSystems *)calloc(1, sizeof(SolarSystems));
    if (ss == NULL) {
        fprintf(stderr, "Error: no se pudo reservar los sistemas solares.\n");
        ecs_world_free(world);
        return 1;
    }

    TrailBuffer *tb = (TrailBuffer *)calloc(1, sizeof(TrailBuffer));
    if (tb == NULL) {
        fprintf(stderr, "Error: no se pudo reservar las estelas.\n");
        free(ss);
        ecs_world_free(world);
        return 1;
    }

    /* FLAG_WINDOW_HIGHDPI: en pantallas con escalado (125%, 150%) hace que el
     * framebuffer sea del tamano fisico real mientras las coordenadas siguen
     * siendo logicas, o sea estrellas nitidas sin tocar el codigo de dibujo. */
    unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI;
    if (cfg.vsync && cfg.targetFps == 0) {
        flags |= FLAG_VSYNC_HINT;
    }
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(cfg.width, cfg.height, "Screensaver ECS - estrellas y sistemas solares");
    if (!IsWindowReady()) {
        fprintf(stderr, "Error: no se pudo crear la ventana.\n");
        free(tb);
        free(ss);
        ecs_world_free(world);
        return 1;
    }
    /* Con vsync NO se llama a SetTargetFPS: los dos limitadores juntos se
     * interfieren (el sleep de raylib hace perder el intercambio de buffer y
     * vsync espera el refresco siguiente, cayendo a la mitad del refresco). */
    if (!cfg.vsync || cfg.targetFps > 0) {
        SetTargetFPS(cfg.targetFps);
    }
    SetExitKey(KEY_ESCAPE);

    if (cfg.fullscreen) {
        ToggleBorderlessWindowed();
        HideCursor();
    }

    Rng rng;
    rng_seed(&rng, seed);

    StarField sf;
    int curW = GetScreenWidth();
    int curH = GetScreenHeight();
    build_scene(world, ss, &sf, tb, &rng, &cfg, curW, curH);

    /* Cargada solo si se pidio: modelos y textura viven en GPU/heap propios,
     * nada que ver con World/SolarSystems/TrailBuffer. Se carga despues de
     * build_scene para que la escena (con la misma semilla) sea identica se
     * pida o no --deadstar. */
    DeathStar deathstar;
    if (cfg.deadstar) {
        deathstar_load(&deathstar, &rng);
        deathstar_center(&deathstar, (float)curW, (float)curH);
    }

    const Color bg = BLACK; /* fondo completamente negro, sin tinte */

    int  showHud    = cfg.hud;
    int  showRings  = cfg.rings;
    int  showTrails = cfg.trails;
    int  paused     = 0;
    int  cursorHidden = cfg.fullscreen;
    float simTime   = 0.0f;
    long  frame     = 0;
    double benchmarkStart = 0.0;
    double benchmarkNextSample = 0.0;
    double benchmarkSampleStart = 0.0;
    double benchmarkElapsed = 0.0;
    long benchmarkFrames = 0;
    long benchmarkSampleFrames = 0;
    int benchmarkCount = 0;
    int benchmarkMin = 0;
    double benchmarkSecondMin = 0.0;
    long benchmarkSum = 0;

    /* Medicion del coste de los sistemas de actualizacion, separado del render:
     * es la parte que se paralelizaria, asi que sirve de linea base. */
    double updateAccum  = 0.0;
    long   updatedFrames = 0; /* los fotogramas en pausa no cuentan en la media */
    const double wallStart = GetTime();

    long captureAt = (cfg.frames > 0) ? (cfg.frames - 1) : 150;
    if (captureAt < 1) {
        captureAt = 1; /* frame se compara despues de incrementarse, nunca vale 0 */
    }

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) {
            dt = 0.05f; /* un tiron del sistema no debe teletransportar planetas */
        }

        if (IsKeyPressed(KEY_H))     showHud    = !showHud;
        if (IsKeyPressed(KEY_O))     showRings  = !showRings;
        if (IsKeyPressed(KEY_T))     showTrails = !showTrails;
        if (IsKeyPressed(KEY_P))    paused     = !paused;
        if (IsKeyPressed(KEY_R)) {
            seed = rng_u32(&rng);
            rng_seed(&rng, seed);
            build_scene(world, ss, &sf, tb, &rng, &cfg, curW, curH);
            if (cfg.deadstar && !cfg.deadstarStatic) {
                deathstar_reset(&deathstar);
                deathstar_center(&deathstar, (float)curW, (float)curH);
            }
        }
        if (IsKeyPressed(KEY_F)) {
            ToggleBorderlessWindowed();
            cursorHidden = !cursorHidden;
            if (cursorHidden) HideCursor(); else ShowCursor();
        }


        /* Cualquier cambio de resolucion (F o arrastrar el borde) invalida la
         * rejilla de sistemas, asi que se recoloca con la misma semilla. */
        const int sw = GetScreenWidth();
        const int sh = GetScreenHeight();
        if (sw != curW || sh != curH) {
            curW = sw;
            curH = sh;
            rng_seed(&rng, seed);
            build_scene(world, ss, &sf, tb, &rng, &cfg, curW, curH);
            if (cfg.deadstar) {
                deathstar_reset(&deathstar);
                deathstar_center(&deathstar, (float)curW, (float)curH);
            }
        }

        if (!paused) {
            const double t0 = GetTime();

            simTime += dt;
            active_sys_spawn_stars(world, &sf, &rng, dt);
            active_sys_drift(world, ss, dt);
            if (cfg.deadstar) {
                const float moveX = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
                const float moveY = (float)IsKeyDown(KEY_S) - (float)IsKeyDown(KEY_W);
                deathstar_move(&deathstar, moveX, moveY, (float)curW, (float)curH, dt);
                if (IsKeyPressed(KEY_SPACE)) {
                    deathstar_fire(&deathstar, ss, &rng);
                }
                deathstar_update(&deathstar, world, ss, tb, &rng, cfg.systems,
                                 (float)curW, (float)curH, dt);
            }
            active_sys_twinkle(world, simTime);
            active_sys_orbit(world, dt);
            active_sys_trails(world, tb, dt);
            sf.liveStars -= active_sys_lifetime(world, dt);

            updateAccum += GetTime() - t0;
            updatedFrames++;
        }

        BeginDrawing();
        ClearBackground(bg);
        active_sys_render(world, ss, tb, showRings, showTrails);
        if (cfg.deadstar) {
            deathstar_render(&deathstar, curW, curH);
        }
        DrawFPS(curW / 2 - 40, 8); /* siempre visible, arriba, aunque el HUD este apagado */
        if (showHud) {
            draw_hud(world, ss, &sf, cfg.deadstar ? &deathstar : NULL, seed, paused);
        }
        EndDrawing();

        frame++;

        if (cfg.benchmark) {
            const double now = GetTime();
            if (benchmarkStart == 0.0) {
                if (now - wallStart >= BENCHMARK_WARMUP_SECS) {
                    benchmarkStart = now;
                    benchmarkSampleStart = now;
                    benchmarkNextSample = now + 1.0;
                    updateAccum = 0.0;
                    updatedFrames = 0;
                }
            } else {
                benchmarkFrames++;
                benchmarkSampleFrames++;
                if (benchmarkCount < BENCHMARK_SAMPLES &&
                    now >= benchmarkNextSample) {
                    const int fps = GetFPS();
                    const double sampleElapsed = now - benchmarkSampleStart;
                    const double secondFps = (double)benchmarkSampleFrames / sampleElapsed;
                    benchmarkSum += fps;
                    if (benchmarkCount == 0 || fps < benchmarkMin) {
                        benchmarkMin = fps;
                    }
                    if (benchmarkCount == 0 || secondFps < benchmarkSecondMin) {
                        benchmarkSecondMin = secondFps;
                    }
                    benchmarkCount++;
                    benchmarkSampleStart = now;
                    benchmarkSampleFrames = 0;
                    benchmarkNextSample += 1.0;
                }
                benchmarkElapsed = now - benchmarkStart;
                if (benchmarkElapsed >= BENCHMARK_DURATION_SECS) {
                    break;
                }
            }
        }

        if (cfg.screenshot != NULL && frame == captureAt) {
            /* No se usa TakeScreenshot: en 5.5 multiplica el tamano de render
             * por la escala DPI otra vez y devuelve una imagen mas grande que
             * el framebuffer, con bandas negras. LoadImageFromScreen si acierta. */
            Image shot = LoadImageFromScreen();
            if (!ExportImage(shot, cfg.screenshot)) {
                fprintf(stderr, "Aviso: no se pudo escribir '%s'.\n", cfg.screenshot);
            }
            UnloadImage(shot);
        }
        if (cfg.frames > 0 && frame >= cfg.frames) {
            break;
        }
    }

    const double elapsed = GetTime() - wallStart;
    if (cfg.deadstar) {
        deathstar_unload(&deathstar);
    }
    CloseWindow();

    if (frame > 0 && elapsed > 0.0) {
        const double averageUpdateMs = (updatedFrames > 0)
            ? updateAccum * 1000.0 / (double)updatedFrames
            : 0.0;
        printf("\n--- resumen ---\n");
        printf("Version            : %s (%d hilo(s))\n", SYSTEMS_MODE, SYSTEMS_THREADS());
        printf("Fotogramas        : %ld en %.2f s  (%.1f FPS medio)\n",
               frame, elapsed, (double)frame / elapsed);
        if (updatedFrames > 0) {
            printf("Actualizacion ECS : %.3f ms/fotograma (media, sin render)\n",
                   averageUpdateMs);
        }
        printf("Sistemas solares  : %d   Planetas: %d\n", ss->count, ss->totalPlanets);
        printf("Estrellas vivas   : %d / %d\n", sf.liveStars, sf.targetStars);
        printf("Entidades         : %u vivas, pico de indice %u de %u\n",
               world->alive, world->highWater, world->capacity);
        printf("Semilla           : %u\n", seed);
        if (cfg.deadstar) {
            printf("Deadstar          : %d sistema(s) destruido(s)\n", deathstar.kills);
        }
        if (cfg.benchmark && benchmarkElapsed > 0.0 && benchmarkCount > 0) {
            const double averageFps = (double)benchmarkFrames / benchmarkElapsed;
            const double getFpsAverage = (double)benchmarkSum / (double)benchmarkCount;
            printf("Benchmark         : %.3f s, %ld fotogramas, %.2f FPS medio\n",
                   benchmarkElapsed, benchmarkFrames, averageFps);
            printf("Intervalos de 1 s : minimo %.2f FPS, %d muestras\n",
                   benchmarkSecondMin, benchmarkCount);
            printf("GetFPS            : %.2f FPS medio, minimo %d\n",
                   getFpsAverage, benchmarkMin);
            printf("BENCHMARK_CSV,%s,%d,%u,%d,%d,%d,%d,%.6f,%ld,%.3f,%.3f,%.3f,%d,%d,%.6f\n",
                   SYSTEMS_MODE, SYSTEMS_THREADS(), seed,
                   cfg.systems, cfg.stars, cfg.width, cfg.height,
                   benchmarkElapsed, benchmarkFrames, averageFps,
                   benchmarkSecondMin, getFpsAverage, benchmarkMin,
                   benchmarkCount, averageUpdateMs);
        }
    }

    free(tb);
    free(ss);
    ecs_world_free(world);
    return 0;
}
