# Estado del proyecto (para retomar en otra sesión)

Última revisión: 2026-08-27. Build limpio (`make`, cero warnings con
`-Wall -Wextra -Wpedantic`). La posición de cada sistema ya no depende de su
ancla (ver "El diseño de anclas" abajo) y hay una modalidad nueva,
`--deadstar` (ver esa sección más abajo, **ya en su v7**: el ojo gira pegado
al cuerpo, hundido con un bisel de socket, se frena y dispara DOS veces por
vuelta — apenas aparece, izquierda, y al cruzar el frente exacto, derecha —
a un punto lejos del centro, y la estación es más chica que antes; no lleva
`SECS`). Además, en una vuelta aparte: paletas más vívidas, planetas
teñidos por la calidez de su sol, y sistemas solares con capas de
profundidad fijas (tamaño/brillo por capa, no solo orden de dibujo) — ver
"Colores vívidos + ... + capas de profundidad" más abajo. **Nota**: los
nuevos sorteos de RNG corren la secuencia — cualquier `--seed` de antes de
esta sesión produce una escena distinta ahora, no es una regresión.

Historial de esta sesión: se retomó con el v4 implementado pero sin
committear, y con dos `fprintf(stderr, "DBG ...")` de depuración sueltos en
`deathstar_update` (más un `g_dbgFrame` global en `main.c` solo para
alimentarlos) — se detectaron y quitaron antes de commitear, confirmando
primero con capturas que el disparo por rotación funciona sin ellos. Sobre
ese commit, el usuario pidió 4 ajustes de sensación (ver v5 abajo): ojo
encajado en vez de flotando, transición suave al dar la vuelta en vez de
pop, más lento cerca del frente, y disparos más lejos de la estación.

## Qué es

Screensaver en C11 + raylib: campo de estrellas de fondo + N sistemas
solares, arquitectura ECS orientada a datos (SoA, ver `src/ecs.h`). Ver
`README.md` para instalar/compilar en macOS y Windows.

## Arquitectura actual del movimiento (la parte que más cambió)

Hay dos niveles de órbita, con el mismo modelo matemático (ángulo + radio +
velocidad angular alrededor de un centro fijo) aplicado en cada nivel:

1. **Planeta alrededor de su sol** — sin cambios desde el inicio.
   `sys_orbit` (`src/systems.c`), usa `ocx/ocy/orx/ory/oang/ospd` por
   entidad en el `World`.
2. **Sistema completo alrededor de un "ancla" de pantalla** — `sys_drift`
   (`src/systems.c`), usa `orbRad/orbAng/orbSpd` por sistema en
   `SolarSystems` (`src/spawn.h`).

### El diseño de anclas, tal como quedó (importante para no repetir vueltas)

- **Dos anclas fijas**, una en el centro de cada mitad de pantalla:
  `anchorX[0]=W*0.25, anchorY[0]=H*0.5` (izquierda) y
  `anchorX[1]=W*0.75, anchorY[1]=H*0.5` (derecha). `src/spawn.c:165-169`.
- La **posición** de cada sistema se decide primero e independiente del
  ancla: rejilla (`cols/rows/cellW/cellH`, ya calculada para separar
  sistemas) + jitter. `src/spawn.c:204-210`.
- La **asignación de ancla** es un reparto barajado (Fisher-Yates sobre un
  arreglo `n/2`/`n/2`, no un volado independiente por sistema — con N chico
  los volados independientes pueden caer muy desparejos por azar, un log
  real dio 3 contra 9). `src/spawn.c:185-202`.
- El **radio y ángulo de órbita** salen de la geometría real entre la
  posición sorteada y el ancla sorteada (`dx,dy` → `sqrtf`/`atan2f`,
  `src/spawn.c:212-217`). Por eso un sistema nacido en una mitad **sí puede**
  terminar orbitando el ancla de la otra y cruzar la línea media — es
  exactamente el comportamiento pedido por el usuario. Ya no hay tope de
  radio por ancla (el bloque `reach`/`anchorMinR`/`anchorMaxR` se eliminó).
- Consecuencia aceptada explícitamente por el usuario: un sistema con ancla
  lejana tiene radio de órbita grande y puede pasar temporadas fuera de
  pantalla (desaparece por el borde y vuelve). No es un bug.
- Velocidad angular ya **no** es un rad/s fijo (con radios que ahora varían
  mucho, ~0 a ~1000px, un rad/s fijo mandaría los sistemas de radio grande a
  volar). Se deriva de una velocidad lineal fija (`rng_range(rng, 25, 70)`
  px/s) dividida entre el radio, acotada a `[0.03, 0.5]` rad/s con signo
  aleatorio. `src/spawn.c:219-223`.

### Decisiones ya tomadas (no re-litigar sin que el usuario lo pida)

Por si otra sesión (u otro agente) lee un mensaje de usuario ambiguo
parecido y tiene la tentación de "mejorar" esto de nuevo — ya se dio esta
vuelta completa:

1. Se probó un modelo de **ancla central única** (un solo punto en el medio
   de la pantalla) para que los sistemas cruzaran ambas mitades. El usuario
   lo rechazó explícitamente: quiere exactamente dos orígenes fijos, uno por
   mitad. **Actualización 2026-08-25**: sí se quiere que el sistema pueda
   verse en una mitad mientras orbita el ancla de la otra (la posición ya no
   depende del ancla asignada, ver arriba) — lo que el usuario rechazó fue el
   ancla única, no el cruce de mitades. Aceptado a propósito: con ancla
   lejana el radio de órbita puede sacar al sistema de pantalla por momentos.
2. Se probó un **volado independiente por sistema** (`rng_below(rng,2)`).
   Es correcto (la posición no influye en la elección, verificado con logs),
   pero el usuario lo rechazó porque el split puede verse desparejo por
   azar. Se reemplazó por el reparto barajado descrito arriba.
3. Ya se ajustó varias veces la duración de la estela (`TRAIL_LEN` en
   `src/systems.h`, hoy en 120 ≈ 5s a `TRAIL_HZ=24`) y la velocidad angular
   — quedaron en los valores de arriba tras ida y vuelta explícita del
   usuario. Si se pide "más rápido/lento" o "más grande/chica la estela" de
   nuevo, es un ajuste de constante, no un rediseño.
4. El fondo es negro puro (`BLACK` de raylib, `src/main.c:308`) — no volver
   a un tinte de color.
5. Paletas de color separadas a propósito: estrellas de fondo = fría
   (blanco/azul), soles = cálida (amarillo/naranja/rojo) — para que no se
   confundan visualmente (`src/spawn.c:5-31`).
6. `DrawFPS()` nativo de raylib, siempre visible arriba-centro, independiente
   del toggle de HUD (`src/main.c`, busca `DrawFPS`).

## Modalidad `--deadstar`

Estrella de la Muerte **3D** (`Camera3D` + `Model`, `GenMeshSphere` para el
cuerpo, `GenMeshCylinder` para el ojo) en el centro de pantalla. La bandera
ya **no** lleva `SECS`: dispara automáticamente **una vez por vuelta**, al
punto al azar de toda la pantalla, en el instante exacto en que el ojo cruza
el frente (ver v4 abajo). Si el punto cae dentro del radio de un sistema, lo
destruye (explosión) y ese sistema **reaparece solo** más tarde (población
nunca llega a cero, respawn cada `DS_RESPAWN_SECS`=6s si hace falta). El
disparo **puede fallar** a propósito — el punto es al azar, no el sistema.

**Historial de idas y vueltas sobre 3D vs 2D — leer antes de tocar el
render de la estación:**

- v1 (3D): el usuario la rechazó — se veía "rara", el sombreado falso (dos
  círculos 2D diagonales sobre la esfera, sin shader) se veía feo, y el ojo
  rotaba en 3D para apuntar al objetivo pero al proyectar a 2D se leía como
  "dispara al frente y no le pega a nada".
- v2 (2D, sin `Camera3D`/`Model`): resolvió lo anterior pero el usuario
  tampoco la quiso — "se ve raro el 2D" no fue el comentario, fue al revés:
  pidió volver a 3D.
- **v3 (3D, primera vuelta que el usuario aceptó)**: aplica las lecciones de
  v1 sin volver a sus errores — sombreado muy restringido (dos círculos
  oscuros suaves pegados al borde inferior) y ojo que vive a un lado fijo
  (izquierda o derecha), **alternando de lado en cada disparo**, con timer
  fijo (`--deadstar SECS`).
- **v4 (3D, el ojo ya no alterna, gira con el cuerpo)**: el
  usuario pidió que el ojo fuera un punto real de la superficie en vez de
  "flotar" a un lado independiente del spin. Cambios sobre v3:
  1. El ojo está **fijo a un punto del cuerpo** (`DS_DISH_LOCAL_AZ_DEG=0°` de
     azimut local, `DS_DISH_EL_DEG=20°` de elevación) y **gira con el spin**
     (`ds_place_dish` se llama cada frame, azimut mundial =
     azimut local + `ds->spin`). Ya no hay `dishSide`.
  2. **Un disparo por vuelta**: se detecta el cruce de 360°→0° del spin
     (`DS_SPIN_RATE_DEG`=36°/s → una vuelta cada 10s) *antes* de envolver el
     ángulo, para no perderlo nunca; ese instante es exactamente cuando el
     ojo mira de frente a cámara. Ya no hay `ds->interval`/`ds->timer` en
     IDLE — el ritmo de disparo lo fija solo la velocidad de rotación.
  3. **Culling del ojo de canto**: con el ojo pegado al cuerpo, en ciertos
     ángulos de rotación el cilindro achatado se ve de canto y degenera en
     una astilla oscura pegada al borde de la silueta (se reportó como
     "mancha gris"; confirmado tiñéndolo de rojo para aislarlo). Se resolvió
     con dos cambios: (a) no dibujar el ojo cuando su normal apunta lejos de
     cámara (`ds->dishPos.z < ds->worldR * DS_DISH_CULL_Z`, umbral 0.38);
     (b) pre-rotar 90° en X el `model.transform` del cuerpo en
     `deathstar_load` — `GenMeshSphere` trae los polos sobre Z (mirando a
     cámara) por defecto, así que sin esta pre-rotación el spin en Y
     tumbaba la esfera en vez de girarla como planeta, y la trinchera
     ecuatorial pasaba por configuraciones de canto todo el tiempo. Con los
     polos ya fijos arriba/abajo, el spin en Y es un giro limpio de planeta.
  4. Ejes de textura del cuerpo reverificados empíricamente con los polos ya
     fijos (antes de la pre-rotación, cualquier prueba de ejes era ambigua
     porque la esfera tumbaba): X constante → paralelo (anillo horizontal,
     estable con el giro); Y constante → meridiano (gira con el spin). La
     trinchera ecuatorial pasó a ser una banda de X constante en
     `texW/2` (antes era Y constante) para que quede como anillo horizontal
     estable en vez de barrer configuraciones de canto.
  5. El ojo tiene textura real (`ds_build_dish_skin`): degradado radial
     concéntrico (128×128) horneado en un `Texture2D`, aplicado a la cara
     del cilindro achatado.
  6. Las luces amarillas cubren **todo el círculo**: en `ds_build_skin` se
     siembran en todo el rango de `v` de la textura de la esfera (0 a
     `texH`), no solo cerca del ecuador.
- **v5 (3D, sensación de disparo)**: sobre v4 el usuario pidió
  4 ajustes de "feel", no de mecánica:
  1. **Ojo encajado, no flotando**: antes el ojo (un disco plano tangente a
     la esfera) quedaba con el borde ligeramente proud de la superficie por
     geometría pura (una tangente plana siempre se separa de una superficie
     convexa lejos del punto de contacto). Se hundió `ds->dishPos` un poco
     por debajo de `worldR` (`DS_DISH_SINK_FRAC`) para que la esfera recorte
     el ojo con el z-test real. **Cuidado con la magnitud**: se probó primero
     con 0.05 (5% de `worldR`) y el ojo entero desapareció detrás del casco
     (occlusion total, confirmado con capturas a `spin≈0` exacto vía
     `--frames 1-2`); 0.01–0.03 no mostró diferencia visible por si solo a
     esta escala de pantalla. La solucion no fue solo el hundido: ver punto 2.
  2. **Bisel de socket**: en vez de perseguir el hundido "correcto" por
     geometria, se dibuja el MISMO modelo del ojo otra vez, mas ancho
     (escala x1.6 en su plano) y oscuro/traslucido (`{10,12,10}`, alpha
     ~0.65), **antes** del ojo real — su borde exterior si queda recortado
     por la esfera (radio mayor = mas sagita), leyendose como un cerco oscuro
     alrededor del ojo, ojo real visible encima intacto. Bug encontrado y
     arreglado en esta vuelta: dibujar el bisel a la MISMA posicion que el
     ojo (solo escalado) causa z-fighting (patron de rayas parpadeando,
     visible en capturas) porque ambos discos quedan coplanares; se hundio el
     bisel un poco mas (`DS_DISH_BEZEL_EXTRA_SINK`) para separarlo en
     profundidad del ojo real.
  3. **Mas lento cerca del frente**: la velocidad de giro ya no es constante
     (`ds_spin_rate_deg`): crucero `DS_SPIN_RATE_DEG` lejos del frente,
     frenada hasta `DS_SPIN_RATE_MIN` en una zona de `DS_FRONT_EASE_DEG`
     grados a cada lado de spin=0, con *smoothstep* (no un quiebre). Antes,
     durante los ~1.25s de CHARGE+FIRE el ojo seguia girando a velocidad
     plena y se corria ~45° del frente mientras cargaba/disparaba; ahora casi
     no se mueve durante esa ventana.
  4. **Disparos mas lejos**: el objetivo ya no es uniforme en toda la
     pantalla (`ds_pick_aim`) — se resamplea (hasta 20 intentos) hasta caer
     fuera de un radio minimo del centro de pantalla
     (`DS_AIM_MIN_DIST_FRAC` del lado corto), para que el rayo nunca sea un
     tiro corto pegado a la estacion.
  - **Nota de verificacion util para la proxima vuelta en esto**: el timing
    real (`GetFrameTime()`) hace que `--frames N` no ubique un spin
    predecible entre corridas (el FPS headless vario 70-210 en esta sesion).
    `--frames 1` o `2` da spin~0 (frente) de forma confiable porque `spin`
    arranca en 0 en `deathstar_load`; para comparar geometria a angulo fijo
    (como los hundidos de sink) es mucho mas confiable que barrer frames al
    azar esperando acertarle al angulo deseado.
- **v6 (3D, la actual — disparo atado a la aparicion, no al frente; nave mas
  chica)**: 3 pedidos mas del usuario sobre v5:
  1. **Frenar apenas aparece, no en un umbral aparte**: `DS_FRONT_EASE_DEG`
     (55°, ajustado a ojo en v5) se reemplazo por `ds->frontEaseDeg`,
     calculado UNA vez en `deathstar_load` a partir de la geometria real
     (`DS_DISH_CULL_Z`, `DS_DISH_SINK_FRAC`, `DS_DISH_EL_DEG` — formula:
     `cos(spin) = CULL_Z / ((1-sink)*cos(el))`, despejado con `acosf`; da
     ~65.36° con los valores actuales). Asi la frenada y el umbral de
     visibilidad SIEMPRE coinciden, no hay dos numeros ajustados a mano por
     separado que se puedan desincronizar si se retoca uno y no el otro.
  2. **Disparar apenas aparece, no al llegar al frente exacto**: el trigger
     de `deathstar_update` ya no es el cruce de spin=360→0 (`atFront`); es el
     flanco de subida de una nueva funcion compartida, `ds_dish_fade(ds)`
     (fraccion de visibilidad 0-1, la misma logica que ya usaba el render
     para el alpha, ahora extraida a una funcion que usan AMBOS: update para
     disparar y render para dibujar — una sola fuente de verdad). Se mide
     ANTES de mover el ojo (dishPos, todavia con la posicion del frame
     anterior) y DESPUES (ya con `ds->spin` avanzado); `fadeBefore<=0 &&
     fadeAfter>0` = acaba de aparecer. Con esto el ciclo completo
     IDLE→CHARGE→FIRE→IDLE queda casi enterito dentro del tramo en que el
     ojo esta apareciendo (no en el pico de brillo maximo) — verificado con
     un arnes headless (`deathstar_update` en bucle, dt fijo, sin GPU, mismo
     patron que describe la seccion de abajo): dispara en spin≈295°
     (=360-65.36, justo el umbral de aparicion), resuelve en spin≈337°,
     **23° antes** de llegar al frente exacto.
  3. **Estacion mas chica**: se veia demasiado grande contra los sistemas
     solares. `frac` (fraccion de pantalla que ocupa `worldR`, en
     `deathstar_load`) bajo de 0.30 a 0.19 — un numero a ojo, ajustar si
     "mas/menos grande" se pide de nuevo. Todo lo demas (`dishR`, `dishH`,
     etc.) es fraccion de `worldR`, asi que escala junto sin tocar nada mas.
  - **Nota de verificacion**: para probar SOLO la logica de disparo/frenado
    sin GPU ni ventana, un arnes que linkea `ecs.c spawn.c systems.c
    deathstar.c` (deathstar.c se puede linkear aunque tiene simbolos de
    raylib GPU — el ejecutable solo falla si de verdad LLAMA a
    `deathstar_load`/`render`, que usan GPU; `deathstar_update` no) funciona
    perfecto: `memset` un `DeathStar` a cero, fijar a mano `worldR`, `dishR`
    y `frontEaseDeg` (misma formula de arriba) y llamar `deathstar_update`
    en bucle con `dt` fijo (p.ej. 1/60). Mucho mas confiable que adivinar
    `--frames N` contra un FPS real que en este entorno vario 60-200+ entre
    corridas (el ciclo de disparo completo son ~75 frames de 60fps sobre un
    total de ~740 frames por vuelta — facil de saltarse de largo adivinando).
- **v7 (3D, la actual — dos disparos por vuelta)**: el usuario pidio
  disparar tambien al cruzar el frente exacto (ademas del disparo de
  aparicion de v6), y que cada disparo quede restringido al lado de
  pantalla donde esta el ojo en ese instante.
  1. **Segundo trigger**: se reintrodujo el booleano de cruce de spin
     360°→0° que v6 habia quitado (`atMiddle`, capturado ANTES de restar
     360, mismo patron que el `atFront` de v4/v5). El trigger de
     `deathstar_update` paso de `if (justAppeared ...)` a
     `if ((justAppeared || atMiddle) ...)`. Sin riesgo de que los dos
     disparos se pisen: el transito de aparicion (`spin≈294.6°`) a frente
     (`spin≈0°`) integrando `ds_spin_rate_deg` da **~3.8s**, contra
     `DS_CHARGE_SECS+DS_FIRE_SECS=1.25s` — de sobra. El guard existente
     `ds->phase==DS_IDLE` alcanza solo, no hizo falta agregar nada mas: en
     el peor caso un frame raro se saltaria un disparo para esa vuelta, no
     corrompe estado.
  2. **Lado de pantalla por disparo**: `ds_pick_aim` gano un parametro
     `int rightHalf` que restringe el rango de muestreo de X a
     `[0,cx]`/`[cx,screenW]` (la distancia minima al centro sigue siendo
     resampleo best-effort, el lado es una restriccion dura del rango). Se
     pasa `atMiddle` directo como `rightHalf` — **no se proyecta
     `ds->dishPos` con `GetWorldToScreen`** para decidir el lado, a
     proposito: el lado sale de CUAL disparo es, por construccion (el de
     aparicion siempre cae en `spin∈(180°,360°)`, `sin(spin)<0` ->
     pantalla-izquierda ya que la camara mira desde `+Z` con `up=+Y`, o sea
     mundo `+X` es pantalla-derecha; el de cruce de frente siempre arranca
     en `spin` pequeno y positivo justo despues de envolver, `sin(spin)≥0`
     -> pantalla-derecha). Proyectar seria ademas incorrecto en la practica:
     `GetWorldToScreen` llama a `GetScreenWidth()` internamente, que
     devuelve 0 sin `InitWindow` — hubiera roto el arnes headless de abajo
     en silencio (compila y linkea igual, solo los numeros salen mal).
  3. **Verificacion**: se extendio el arnes headless (mismo patron de v6,
     `ecs.c spawn.c systems.c deathstar.c`, `dt` fijo) para contar
     transiciones `IDLE→CHARGE` por vuelta e imprimir `ds->spin`/`ds->aimX`
     en cada una. Confirmado en una corrida de 8 disparos: alternan
     limpiamente `spin≈295°→aimX en mitad izquierda` /
     `spin≈0°→aimX en mitad derecha`, sin excepciones, con el espaciado
     esperado (~515 frames de 60fps entre aparicion y cruce, ~515 frames
     entre cruce y la proxima aparicion). La rotacion nunca se detiene
     (`DS_SPIN_RATE_MIN=6` sigue siendo un piso, no cero) — eso ya estaba
     bien desde v5/v6, el usuario solo pidio confirmarlo, no cambiarlo.

**Bug real encontrado y arreglado en esta vuelta (dejar documentado, es
sutil): NO usar `DrawMesh(mesh, material, MatrixMultiply(MatrixRotate(...),
MatrixTranslate(...)))` para dibujar el ojo**, aunque las notas de
arquitectura originales del usuario sugerían exactamente eso. Esa forma
corrompía el render de forma intermitente pero reproducible: la esfera del
cuerpo salía gigante y recortada en una esquina en frames concretos (frame 60
y 260 de una corrida con semilla 42 lo reprodujeron de forma determinista en
reintentos). Se aisló con una prueba binaria — quitando solo el `DrawMesh`
del ojo, el cuerpo volvía a verse perfecto — así que no era un problema de
los valores de la matriz (se verificaron a mano, finitos y sanos) sino de la
llamada `DrawMesh` cruda en sí (sospecha: algo en cómo rlgl maneja el stack
de matrices al mezclar `DrawModelEx` con `DrawMesh` manual dentro del mismo
`BeginMode3D`). La solución fue usar `DrawModelEx(ds->dish, ds->dishPos,
ds->dishRotAxis, ds->dishRotAngle, {1,1,1}, WHITE)` — la misma función ya
usada (y ya probada) para el cuerpo — en vez de compilar la matriz a mano.
Si en el futuro hace falta un control de transformación que `DrawModelEx` no
ofrezca, investigar esto a fondo antes de volver a `DrawMesh` manual.

Todo en `src/deathstar.c/.h`, cero cambios de comportamiento sin la bandera.
Piezas que sí tocan el resto del ECS, porque no existían antes de la sesión
en que se agregó `--deadstar`:

- `spawn_one_system` / `solar_system_remove` (`src/spawn.c`) — alta/baja
  incremental de un sistema. `solar_system_remove` compacta la tabla de
  anillos aplanada (`ringFirst`/`ringTotal`) y hace swap-remove del slot; sin
  la compactación `ringTotal` nunca recicla y a los pocos cientos de muertes
  se topa en `MAX_PLANETS_TOTAL`. `spawn_one_system` reusa el layout de
  rejilla que `spawn_solar_systems` ahora guarda en `SolarSystems`
  (`gridCols/gridRows/cellW/cellH/cellR/sunRad/planetRef/jitter`) y asigna el
  ancla con menos sistemas (empate = volado) para no romper el balance 50/50.
- `trails_drop_system` / `trails_add_system` (`src/systems.c`) — swap-remove
  / alta de columnas en `TrailBuffer` sin tocar `trails_init` (que borraría el
  historial de *todos* los sistemas en cada muerte). Hay que llamar
  `trails_drop_system` **antes** de `solar_system_remove`: después ya no se
  sabe qué entidades tenía el sistema `s`.
- Las partículas de explosión **no son entidades del ECS** (a propósito):
  posición analítica (`centro + dir*vel*edad`), sin componente nuevo en
  `World`. Evita una trampa real: `main.c` hace
  `sf.liveStars -= sys_lifetime(...)`, y una partícula con `C_LIFE`
  descontaría estrellas que nunca existieron.

Bug histórico ya no aplicable (dejado como nota si algo similar reaparece):
en v3, `deathstar_load` recibía un `secs` y `deathstar_reset` debía llamarse
*después* de fijarlo en `ds->interval`, porque `reset` sembraba
`timer`/`respawnTimer` a partir de ese campo — invertir el orden dejaba
`timer` en basura de stack y la Estrella de la Muerte no disparaba nunca, sin
warning del compilador. En v4 ya no existe `ds->interval` (IDLE no usa
timer, ver arriba), así que esta clase de bug no puede volver a esta forma
concreta — pero la lección general (no leer un campo de `ds` en `reset`
antes de que `load` lo haya fijado) sigue valiendo si se agrega estado
nuevo.

Geometría de la estación, tal como quedó (3D, `src/deathstar.c`):

- `worldR` (radio del cuerpo, unidades de mundo) sale de
  `frac(0.30) * camDist(6) * tan(fovy(45°)/2)`, **no** de `screenW/H` —
  raylib cubre el alto completo de pantalla con `fovy` en Y sin importar el
  ancho, así que este radio ocupa siempre la misma fracción de pantalla y el
  resize no toca nada.
- Cuerpo: `GenMeshSphere(worldR,24,32)` con textura procedural
  (`ds_build_skin`: paneles + trinchera ecuatorial en X=texW/2 + luces
  amarillas en todo el rango de v), `model.transform` pre-rotado 90° en X
  (ver v4, punto 3) y dibujado con `DrawModelEx` girando en el eje Y
  (`ds->spin`).
- Ojo: `GenMeshCylinder(dishR, dishH, 24)` con textura de degradado radial
  (`ds_build_dish_skin`), hundido en el cuerpo con un bisel oscuro detrás
  (ver v5 arriba, puntos 1-2). Posición/orientación se recalculan cada
  frame, fijas al cuerpo (`ds_place_dish`, ver v4 arriba), dibujado con
  `DrawModelEx` — **no** `DrawMesh` manual, ver el bug documentado abajo —
  con alpha desvanecido en vez de un corte binario al dar la vuelta
  (`DS_DISH_CULL_Z`/`DS_DISH_FADE_Z`, ver v4 punto 3 y v5).
- Rayo: `ds_render_beam` calcula los 8 puntos del borde del ojo en 3D
  (tangente/bitangente a la normal del ojo) y los proyecta con
  `GetWorldToScreen`; el foco de convergencia y el rayo final son 2D puro
  hacia `(aimX,aimY)` real.
- **Nota**: esta seccion documentaba tambien un `ds_render_shading` (sombra
  de dos circulos bajo el cuerpo, ver el historial v3 mas arriba) que ya no
  existe en el codigo — se perdio en algun momento entre v3 y v4 sin que
  esta nota se actualizara. Si hace falta, reintroducirlo es del mismo
  estilo que el bisel del ojo (v5, punto 2): geometria 2D barata post-3D, no
  un shader.

`ds->blastR` (radio de impacto) se ata a `ss->cellR` (no un píxel fijo): se
reescala solo si N cambia.

## Colores vívidos + planetas atados a la calidez del sol + capas de profundidad

Sesión aparte (misma fecha), tres pedidos sobre `src/spawn.c`/`src/spawn.h`/
`src/systems.c`/`src/systems.h`, ninguno toca `--deadstar`.

**Paletas retunadas** (`STAR_PALETTE`/`SUN_PALETTE`/`PLANET_PALETTE`,
`spawn.c:6-60`): más saturadas que antes. La regla vieja (fondo frío,
soles cálidos, bandas que NO se solapan — ver el comentario de arriba, no
re-litigar) queda intacta a propósito: subir saturación separa aún más las
dos bandas, no las acerca. `STAR_PALETTE[0]`/`[1]` siguen duplicados
(peso 2/5 hacia blanco puro, no es un error de copiar-pegar).

**Planetas atados a la calidez del sol**: `SUN_PALETTE` está ordenada
frío→cálido a propósito — el ÍNDICE es el ordinal de calidez que usa
`spawn_system_into_slot` (`warm = sunPal >= SUN_PALETTE_N/2`), no un umbral
de RGB calculado a mano aparte (ese segundo enfoque se descartó explícitamente:
un umbral tuneado contra la tabla de hoy se desincroniza en silencio la
próxima vez que se retoquen los colores — mismo motivo por el que
`ds->frontEaseDeg` en `deathstar.c` dejó de ser una constante suelta). El
índice del sol se sortea ANTES de llamar a `spawn_sun` (mismo orden de RNG
que antes) para poder leerlo en el llamador. `PLANET_PALETTE` es UNA tabla
con dos rangos superpuestos (`PLANET_COOL_LO/HI`, `PLANET_WARM_LO/HI`), no
dos tablas — `templado`/`rocoso` caen en ambos rangos para que ningún
sistema se vea monocromo. Motivo físico, no solo estético: la luz reflejada
por un planeta está teñida por la de su sol.

**Capas de profundidad** (`SYS_LAYER_COUNT=4`, `spawn.h`): cada sistema nace
en una capa fija (0=atrás/chico/tenue, 3=adelante/tamaño y brillo
completos), para que la distancia se vea SIEMPRE, no solo cuando dos
sistemas se cruzan. Puntos importantes si se retoca:

- **Reparto barajado, no `rng_below` independiente por sistema**: un volado
  independiente por sistema es exactamente el error ya litigado y
  rechazado para las anclas (ver "El diseño de anclas" arriba) — con N
  chico puede amontonar casi todo en una capa. `spawn_solar_systems` arma
  `lay[s]=s%SYS_LAYER_COUNT` y lo baraja con el mismo Fisher-Yates que ya
  usa para `assign[]`. `spawn_one_system` imita la lógica de "balde menos
  poblado" que ya usaba para elegir ancla.
- **Spawn escala geometría, render escala brillo — NO al revés**. Se
  descartó a propósito repurposear `w->alpha[e]`/`twBase`/`twAmp` del sol
  para el atenuado por capa: `render_sun_glow` lee `w->alpha[e]` como
  **multiplicador de RADIO** del halo (`r*1.5f*pulse`), no como alpha —
  achicar `twBase` ahí encoge el halo *dentro* del núcleo opaco en vez de
  atenuarlo, y se ve como el halo roto, no como distancia. En cambio: el
  tamaño (radio del sol, radios de órbita, velocidad lineal de deriva) se
  hornea UNA vez al nacer en `spawn_system_into_slot` vía `solar_layer_scale`
  (`sc`); el brillo (glow del sol, alpha de planeta/estela/anillo) se lee
  en el render vía `solar_layer_alpha`, directo de `ss->layer[s]` — las
  dos funciones son `static inline` en `spawn.h`, una sola fuente de
  verdad para ambos lados.
- **Copias locales, nunca mutar `ss->cellR`/`ss->sunRad`/`ss->planetRef`
  in-place**: son la plantilla compartida que reusa todo `spawn_one_system`
  futuro, y `deathstar_update` lee `ss->cellR` directo para `ds->blastR`.
  `spawn_system_into_slot` usa `cellRs`/`sunRadS`/`prRef` (variables locales
  escaladas por `sc`) en su lugar.
- **La formula de Kepler necesita el radio escalado en LOS DOS lados de la
  razón**: `speed = baseSpeed * powf(cellRs / rx, 1.5f)` — `rx` ya sale
  escalado (`cellRs * frac`); si el numerador se dejaba en el `ss->cellR`
  sin escalar, los sistemas de atrás girarían MÁS rápido (razón invertida),
  justo al revés de lo que se pedía. Bug real encontrado en el diseño antes
  de escribir el código, no en pantalla — dejarlo documentado por si se
  toca esta fórmula de nuevo.
- **La pasada opaca es la única donde el orden de dibujo importa de
  verdad**: antes `sys_render` era 4 pasadas GLOBALES intercaladas (todos
  los anillos, todas las estelas, TODOS los soles, TODOS los planetas, en
  orden de entidad ECS) — como los planetas se pintaban enteros después de
  todos los soles, **cualquier planeta de la escena tapaba a cualquier
  sol**, sin importar el sistema; ese era el bug real detrás de "no se ve
  cuál está adelante". Se partió en `render_sun_glow` (aditivo, sigue
  global/sin ordenar — conmuta, no importa el orden), `render_bodies`
  (recorre las `SYS_LAYER_COUNT` capas de atrás hacia adelante, y dentro de
  cada capa dibuja sol+planetas de cada sistema de forma atómica — el
  núcleo del sol se queda a alpha 255 fijo, sin atenuar, a propósito, para
  no tocar un comportamiento que no se pidió cambiar) y `render_specular`
  (aditivo, global). Por baldes de capa, no un sort genérico: con
  `SYS_LAYER_COUNT` chico son 4 barridos filtrados, y dos sistemas de la
  misma capa comparten escala/alpha — no hay un orden real entre ellos que
  un sort pudiera capturar mejor.
- **Estelas**: `trails_append_body` ganó un parámetro `mul` que atenúa
  `cr/cg/cb` antes de guardarlos (así el color viaja con la columna, inmune
  a que `solar_system_remove` reindexe sistemas). `trails_init` se reescribió
  para recorrer sistema por sistema (antes eran dos bloques de copia de
  color duplicados con un escaneo plano) — de paso quedó arreglado que
  dejaba `x[]/y[]` en cero de `calloc` en vez de sembrarlos con la posición
  real, que es lo que `trails_append_body` ya hacía bien.
- **Fuera de alcance a propósito** (`ponytail:` en el código): orden
  intra-sistema (un planeta siempre se dibuja encima de su propio sol,
  nunca detrás según su ángulo de órbita) — barato de agregar con
  `sinf(oang[e])` si algún día se nota, no era lo pedido.

**Verificación**: arnés headless (mismo patrón documentado abajo, linkea
`ecs.c spawn.c systems.c`, sin `deathstar.c`) con 200 ciclos de
remove+spawn confirmando `ss->layer[s] ∈ [0,SYS_LAYER_COUNT)` en todo
momento (agarra un swap-remove al que se le olvide copiar `layer[s]`) más
los invariantes viejos de la tabla de anillos. Visualmente: capturas a
varios seeds/N muestran tamaño y brillo variando claramente sistema a
sistema (no solo en los que se cruzan), y suelos cálidos con planetas
cálidos / soles fríos con mezcla fría-neutra consistente.

**Nota importante para cualquier baseline vieja**: los nuevos sorteos de
RNG (índice de capa, índice de paleta del sol leído antes que antes) corren
la secuencia de números aleatorios — **cualquier `--seed` de antes de esta
sesión ahora produce una escena distinta**. No es una regresión, es
esperado; no comparar capturas viejas de STATUS.md contra el build actual
pixel a pixel.

## Cómo verificar cambios futuros en este subsistema

No hay ventana disponible en este entorno (headless). Patrón usado en toda
la sesión, funciona bien:

```bash
make clean && make            # debe compilar sin warnings
./screensaver 10 --seed 100 --frames 300 --screenshot /tmp/x.png
```

Luego leer el PNG con la herramienta Read de Claude Code (no hace falta
abrir la imagen manualmente).

Para verificar la lógica de asignación/posición sin ventana ni render, se
usó un arnés que linkea directo contra `src/ecs.c src/spawn.c src/systems.c`
(sin `main.c`) y llama `spawn_solar_systems` / `sys_drift` en un bucle
`for`, imprimiendo posiciones o `ss->anchor[s]` a stdout. Es la forma rápida
de confirmar cosas como "¿el split es parejo?" o "¿el sistema se sale de su
mitad?" sin adivinar mirando capturas.

Mismo patrón, usado para verificar `spawn_one_system`/`solar_system_remove`/
`trails_drop_system`/`trails_add_system`: arnés que linkea
`ecs.c spawn.c systems.c` (sigue sin `main.c` ni `deathstar.c`, así que no
hace falta GPU/ventana), 200 ciclos de remove+spawn sobre un sistema al azar,
con `assert` tras cada ciclo verificando que
`sum(planetCount) == ringTotal == totalPlanets`, que los rangos
`[ringFirst[s], +planetCount[s])` son disjuntos y cubren `[0, ringTotal)`,
que toda entidad referenciada está viva, que `tb->bodyCount == count +
ringTotal`, y que `ringTotal` no crece sin límite (prueba de que la
compactación realmente recicla). Importante si se reconstruye este arnés:
`ecs_reset` **no** fija `w->capacity` (eso lo hace `ecs_world_alloc`), así que
hay que llamar `ecs_world_alloc()` y no declarar un `World` a mano — si no,
`ecs_create` devuelve `ECS_INVALID` siempre y todo lo demás falla en cadena
de forma confusa.

Para la máquina de disparo de `--deadstar` (IDLE/CHARGE/FIRE): **no** llamar
`deathstar_load` en un arnés headless — otra vez es 3D, crea texturas/modelos
y necesita contexto GPU real (`InitWindow`). Para probar solo la máquina de
estados sin ventana, inicializar a mano un `DeathStar` con
`memset(&ds,0,sizeof(ds))` y solo los campos que la máquina usa
(`phase/timer/spin/respawnTimer`), y llamar `deathstar_update` en bucle con
un `dt` fijo — `deathstar_update` en sí no toca nada de GPU (el 3D vive solo
en `deathstar_load`/`deathstar_render`). Sirve para confirmar que el cruce de
360° dispara CHARGE→FIRE en el tiempo esperado (`360/DS_SPIN_RATE_DEG`
segundos) y que `kills`/`ss->count` se mueven. `deathstar_render` sí necesita
ventana real (raylib no permite `BeginMode3D`/`DrawModelEx` sin contexto GL).

Para verificar el render 3D en sí (posición del ojo, tamaño/centrado del
cuerpo, sombreado, rayo) no hay atajo sin ventana: hace falta correr
`./screensaver N --deadstar --seed S --frames F --screenshot RUTA` y leer el
PNG. **Importante**: `--frames` cuenta fotogramas reales, no segundos —
`dt` sale de `GetFrameTime()` (tiempo real, no paso fijo), así que a cuántos
frames corresponden 10s de giro depende del FPS real de la corrida (visto
entre ~70 y ~120 FPS en este entorno headless, así que el primer disparo cae
entre frame ~700 y ~1200, no en frame 600 como daría a 60 FPS constantes).
Más simple: capturar con `--hud` y mirar la línea "Estrella de la Muerte:
girando/cargando/FUEGO" para ubicar la fase sin adivinar el frame exacto, o
barrer varios `--frames` hasta ver "Sistemas destruidos" subir.

Ninguno de estos arneses quedó como archivo en el repo (eran temporales, en
`/tmp`); ver el historial de la sesión si hace falta reconstruirlos.

## Qué falta / posibles siguientes pasos

Nada pendiente a medio hacer — todo lo pedido quedó implementado, verificado
y committeado en `main`. Ideas no pedidas explícitamente pero mencionadas de
pasada en el código como `ponytail:` (buscar ese prefijo):

- `src/systems.c`: sin techo de segmentos por frame en el dibujo de estelas
  (`bodyCount*TRAIL_LEN`, peor caso ~196k con N=256 sistemas llenos). Con N
  típico (≤20) sobra margen; si algún día se prueba con N muy alto y se
  nota, dibujar 1 de cada 2 rebanadas.

Fuera de eso, dos sistemas pueden solaparse visualmente si sus órbitas se
cruzan — aceptado a propósito, es un screensaver, no se agregó detección de
colisiones.

- `src/deathstar.c`: `ds_spawn_explosion` descarta la explosión si ya hay
  `DS_MAX_EXPLOSIONS` (8) activas en vez de encolar o reciclar la más vieja.
  Con un disparo por vuelta (~1 cada `360/DS_SPIN_RATE_DEG`=10s, explosión de
  1.6s) nunca se llega a 8 simultáneas; si algún día se sube mucho
  `DS_SPIN_RATE_DEG` y se nota, reciclar la más vieja en vez de descartar el
  disparo nuevo.
- Las texturas procedurales (`ds_build_skin`, `ds_build_dish_skin`,
  `src/deathstar.c`) son un parecido esquemático (paneles + trinchera +
  luces; degradado radial concéntrico), no un modelo fiel a la película. Si
  no convence visualmente, el ajuste va en esas dos funciones.
- El azimut/elevación fijos del ojo (`DS_DISH_LOCAL_AZ_DEG=0`,
  `DS_DISH_EL_DEG=20`), el hundido y su bisel (`DS_DISH_SINK_FRAC=0.03`,
  `DS_DISH_BEZEL_EXTRA_SINK=0.025`, escala `1.6` del bisel — ver v5), la
  velocidad de crucero/mínima del frenado (`DS_SPIN_RATE_MIN=6`,
  `DS_SPIN_RATE_DEG=48` — el ANCHO de la zona, `ds->frontEaseDeg`, ya NO es
  a ojo, ver v6), la distancia mínima de disparo (`DS_AIM_MIN_DIST_FRAC=0.32`),
  el umbral de culling/desvanecido (`DS_DISH_CULL_Z=0.38`, `DS_DISH_FADE_Z=
  0.62` — mover cualquiera de estos dos cambia tambien `ds->frontEaseDeg`,
  son la misma fuente de verdad, ver v6) y el ángulo de cámara/tamaño
  (`frac=0.19`, `camDist=6`, `fovy=45`) son valores a ojo, no medidos contra
  ninguna referencia — ajuste de constante si se pide "más/menos inclinado",
  "más/menos lento", "más/menos lejos" o "más grande/chico".
