#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raylib.h"
#include "ecs.h"
#include "rng.h"
#include "spawn.h"

#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT  720
#define DEFAULT_STARS  1200
#define MAX_SYSTEMS 100
#define ECS_MAX_ENTITIES 262144u
#define MAX_PLANETS_TOTAL 2000
#define MAX_STARS (ECS_MAX_ENTITIES - MAX_PLANETS_TOTAL - MAX_SYSTEMS - 1024u)

typedef struct Config {
    int          systems;
    int          stars;
    int          width;
    int          height;
    unsigned int seed;
    int          fullscreen;
    long         frames;
    const char  *screenshot;
    int          rings;
    int          vsync;
    int          hud;
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
    printf("  --no-vsync         sin sincronia vertical (para medir FPS reales)\n");
    printf("  --hud              arrancar con el panel de datos visible\n");
    printf("  --frames K         salir tras K fotogramas (para pruebas)\n");
    printf("  --screenshot RUTA  guardar un PNG y seguir (para pruebas)\n");
    printf("  -h, --help         esta ayuda\n");
    printf("\nTeclas: H hud | O orbitas | SPACE pausa | R nueva escena | F pantalla | ESC salir\n");
}

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
    cfg->vsync      = 1;
    cfg->hud        = 0;

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
        } else if (strcmp(a, "--no-vsync") == 0) {
            cfg->vsync = 0;
        } else if (strcmp(a, "--hud") == 0) {
            cfg->hud = 1;
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
    return 0;
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
        return 1;
    }

    unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI;
    if (cfg.vsync) {
        flags |= FLAG_VSYNC_HINT;
    }
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(cfg.width, cfg.height, "Screensaver ECS - estrellas y sistemas solares");
    if (!IsWindowReady()) {
        fprintf(stderr, "Error: no se pudo crear la ventana.\n");
        ecs_world_free(world);
        return 1;
    }
    if (!cfg.vsync) {
        SetTargetFPS(0);
    }
    SetExitKey(KEY_ESCAPE);

    if (cfg.fullscreen) {
        ToggleBorderlessWindowed();
        HideCursor();
    }

    Rng rng;
    rng_seed(&rng, seed);

    StarField sf;
    starfield_init(&sf, cfg.stars, (float)GetScreenWidth(), (float)GetScreenHeight());
    
    /* Creamos algunas estrellas estáticas de prueba */
    for (int i = 0; i < sf.targetStars; ++i) {
        spawn_star(world, &rng, sf.screenW, sf.screenH);
    }

    const Color bg = { 6, 8, 18, 255 };

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) {
            dt = 0.05f;
        }

        BeginDrawing();
        ClearBackground(bg);
        EndDrawing();
    }

    CloseWindow();
    ecs_world_free(world);
    return 0;
}
