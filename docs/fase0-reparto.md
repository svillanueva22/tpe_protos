# Fase 0 — Base que compila: reparto en 4 partes

Objetivo de la fase: `make clean && make && make test` pasa **sin warnings** en Linux y
macOS, `socks5d` escucha y cierra limpio con Ctrl+C, y `mgmt-client` se conecta al
servidor. Todavía **no** hay SOCKS: eso es la Fase 1.

Este documento está pensado para que los 4 trabajen **en paralelo**. Antes de arrancar,
leer la sección 1 (acuerdos) y la 2 (orden). Ante cualquier duda, mandan
`docs/20262-r0.txt` (enunciado) y `AGENTS.md`.

---

## 0. Estado de partida (`main` en `f071fd8`, 26/11/2026)

Lo que ya está hecho (PR #2):

- Patches de la cátedra aplicados y movidos a `src/lib/`.
- `src/server/main.c`, `src/server/args.{c,h}` y `src/socks5/socks5nio.c` copiados de
  `docs/` **sin modificar**.
- `Makefile` con `all`, `socks5d`, `test`, `clean` y objetos en `build/`.

Lo que da `make` hoy (macOS):

```
src/lib/selector.c:75..529  sigaction, sigemptyset, sigaddset, sigprocmask, pthread_kill no declaradas
src/lib/selector.c:52       warning: unused parameter 'signal'
src/lib/netutils.c:66,87    MSG_NOSIGNAL no existe en macOS
src/server/main.c:26        'socks5.h' file not found
src/socks5/socks5nio.c:15   'hello.h' file not found
```

Otros pendientes detectados:

- `.gitignore` está vacío (0 bytes).
- `src/lib/foo.dot` es basura del patch 0006.
- `make test` no corre `netutils_test`; no existe `make debug`.
- `mgmt-client` en el Makefile es un `echo`.
- `src/utils/tcpServerUtil.h:15` declara `void handleTCPEchoClient` y el `.c` devuelve `int`.
- La macro `log(...)` de `src/utils/logger.h` choca con `log()` de `<math.h>`.
- `README.md` está en UTF-16.

---

## 1. Acuerdos comunes (no cambiar sin avisar al grupo)

| Tema | Acuerdo |
|------|---------|
| Carpetas | `src/lib/` cátedra · `src/utils/` utils de clase · `src/server/` main de `socks5d` · `src/socks5/` SOCKS (Fase 1) · `src-client/` `mgmt-client` · `build/` objetos |
| Includes | Sin ruta: `#include "selector.h"`. El Makefile pasa `-Isrc/lib -Isrc/server -Isrc/socks5 -Isrc/mgmt -Isrc/utils` |
| Flags | `-std=c11 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L -pthread` (no agregar `_GNU_SOURCE` sin avisar) |
| Macro de log | Se llama **`LOG(level, fmt, ...)`**. La Parte 4 hace el rename; la Parte 3 usa `LOG` desde el día 1 |
| `SIGPIPE` | Se ignora en `main` con `signal(SIGPIPE, SIG_IGN)`. `MSG_NOSIGNAL` queda como `0` si no existe |
| Defaults de red | SOCKS `0.0.0.0:1080` + `[::]:1080` · management `127.0.0.1:8080` |
| Makefile | **Dueño: Parte 2.** Los demás no lo editan: le avisan qué fuentes agregan |
| Ramas | `fase0/lib`, `fase0/build`, `fase0/server`, `fase0/client` → PR a `main` |
| Código de la cátedra | Si se toca, comentar `// FIX: <motivo>`. Atribución en el header de cada archivo |
| Review | Cada PR lo revisa al menos otro integrante antes de mergear |

---

## 2. Orden y dependencias

```
            ┌─ Parte 2a (PR chico, ~5 min) ─┐
main ───────┤                               ├──► todos rebasean sobre main
            └───────────────────────────────┘
               │
               ├── Parte 1  fase0/lib     ─┐
               ├── Parte 2b fase0/build   ─┤  en paralelo
               ├── Parte 3  fase0/server  ─┤
               └── Parte 4  fase0/client  ─┘
                                            │
            merge: 1 → 4 → 3 → 2b (integración final y verificación en Linux)
```

- **Parte 2a va primero**: sin ella `make` falla por `socks5nio.c` y nadie puede compilar.
- **Parte 3 en macOS** necesita el fix de `selector.c` de la Parte 1. Mientras no esté
  mergeado, aplicarlo localmente sin commitearlo (o trabajar en Linux, donde no falla).
- **Parte 4** necesita el servidor de la Parte 3 solo para la prueba final; el cliente se
  puede probar antes contra `nc -l 8080`.

---

## Parte 1 — Librería de la cátedra (`src/lib/`)

**Rama:** `fase0/lib` · **Toca solo:** `src/lib/`

1. `src/lib/selector.c:17`: cambiar `#include <sys/signal.h>` por `#include <signal.h>`
   (`// FIX:` en macOS `sys/signal.h` no declara `sigaction` & cía. con `_POSIX_C_SOURCE`).
   Verificar que `pthread_kill` quede declarado (`<signal.h>` + `<pthread.h>`).
2. `src/lib/selector.c:52`: silenciar el parámetro `signal` sin usar (`(void) signal;`).
3. `src/lib/netutils.c`: arriba del archivo
   ```c
   #ifndef MSG_NOSIGNAL
   #define MSG_NOSIGNAL 0   // FIX: no existe en macOS; SIGPIPE se ignora en main
   #endif
   ```
4. Borrar `src/lib/foo.dot`.
5. Agregar en el header de cada archivo de `src/lib/`:
   `Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA`.
6. Instalar Check (`brew install check` / `sudo apt install check`) y hacer pasar los 6
   tests (`buffer`, `netutils`, `parser`, `parser_utils`, `selector`, `stm`). Si algún
   test falla por un bug real de la cátedra, arreglarlo con `// FIX:` y contarlo en el PR.

**Listo cuando:** los 6 tests pasan en macOS y Linux, y `src/lib/*.c` compila sin warnings
con los flags acordados.

---

## Parte 2 — Build y README

**Rama:** `fase0/build` · **Toca solo:** `Makefile`, `.gitignore`, `README.md`

### 2a — PR inicial (hacerlo antes que nada y avisar al grupo)

1. Sacar `src/socks5/socks5nio.c` de `SOCKS5_SRCS` (vuelve en la Fase 1; dejar comentario).
2. Llenar `.gitignore`: `build/`, `socks5d`, `mgmt-client`, `.vscode/`, `*.dSYM/`.

### 2b — Resto

3. Agregar `netutils_test` a `make test`.
4. Obtener Check con `pkg-config --cflags --libs check` (en macOS con Homebrew las rutas no
   son las default), con fallback a `-lcheck`.
5. `make debug`: igual que `all` con `-fsanitize=address,undefined -fno-omit-frame-pointer`.
   Las pruebas de stress se hacen con `make` normal.
6. `mgmt-client` como target real que compile `src-client/*.c` + `src/utils/{logger,util,tcpClientUtil}.c`.
7. Reescribir `README.md` en **UTF-8**: dependencias (gcc/clang, make, Check), cómo
   compilar, dónde quedan los binarios, cómo ejecutarlos, opciones y dónde está cada
   material (enunciado, informe, RFC del protocolo).
8. Verificar el build en Linux (si nadie tiene, Docker:
   `docker run --rm -v "$PWD":/w -w /w gcc:latest sh -c 'apt-get update && apt-get install -y check pkg-config && make clean all test'`).

**Listo cuando:** `make clean && make && make test` sin warnings en macOS y Linux, y
`make debug` compila.

---

## Parte 3 — Servidor base (`src/server/`)

**Rama:** `fase0/server` · **Toca solo:** `src/server/`

1. Reescribir `src/server/main.c` (hoy es la copia de la cátedra):
   - Sacar `#include "socks5.h"` (línea 26) y `#include "socks5nio.h"` (línea 28).
   - Abrir sockets pasivos **no bloqueantes** (`selector_fd_set_nio`) con `SO_REUSEADDR`:
     SOCKS en IPv4 e IPv6 (`IPV6_V6ONLY=1` en el v6 para que no choquen) y management en
     `127.0.0.1:8080`. Direcciones/puertos desde `args`.
   - Registrar los 3 en el selector con `OP_READ` y un **handler provisorio** que haga
     `accept`, loguee la dirección del cliente con `LOG(INFO, ...)` y cierre. En la Fase 1
     se reemplaza por `socksv5_passive_accept` (hoy en la línea 121).
   - Mantener `close(0)` (línea 61): ahorra un fd (ver límite de `FD_SETSIZE` en `AGENTS.md` §3).
2. Señales: `SIGINT`/`SIGTERM` setean un flag `done` y el loop sale; cierre ordenado
   (`selector_destroy`, `selector_close`, cerrar sockets). Ignorar `SIGPIPE`.
3. `src/server/args.c`:
   - Cambiar `getopt_long` (línea 95) por `getopt` POSIX con el mismo optstring
     `"hl:L:Np:P:u:v"`, y `<getopt.h>` (línea 6) por `<unistd.h>`.
   - Actualizar líneas 49-50: `2026/2 -- Grupo <N>` y la licencia que elija el grupo.
   - Validar puertos (1–65535) y formato `user:pass`, con mensaje de error claro.

**Listo cuando:**

```sh
./socks5d -h                # ayuda
./socks5d -p 1080 &
nc -v localhost 1080        # conecta y el servidor cierra; se ve el log
nc -v ::1 1080              # idem por IPv6
nc -v 127.0.0.1 8080        # management
kill -INT %1                # sale limpio
```

y `make debug` + Ctrl+C no reporta leaks.

---

## Parte 4 — Utils y cliente (`src/utils/`, `src-client/`)

**Rama:** `fase0/client` · **Toca solo:** `src/utils/`, `src-client/`

1. `src/utils/logger.h`: renombrar la macro `log` → `LOG` y actualizar todos los usos en
   `src/utils/*.c` (`grep -n 'log(' src/utils/*.c`).
2. `src/utils/tcpServerUtil.{c,h}`: el echo no se usa en el servidor (es bloqueante).
   Borrar `handleTCPEchoClient` (recomendado) o corregir la firma a `int`.
3. `src-client/main.c` + `src-client/args.{c,h}`:
   - Opciones con `getopt`, coherentes con el servidor: `-h`, `-v`, `-L <addr>`
     (default `127.0.0.1`), `-P <port>` (default `8080`).
   - Conectarse con `tcpClientSocket()` (en el cliente sí puede ser bloqueante), loguear
     que conectó y salir con código 0; si falla, mensaje claro y código ≠ 0.
   - Dejar marcado con `// TODO Fase 3` dónde va el protocolo de management.
4. Avisar a la Parte 2 la lista de fuentes del cliente para el Makefile.

**Listo cuando:** `./mgmt-client -L 127.0.0.1 -P 8080` conecta al `socks5d` de la Parte 3
(o a `nc -l 8080` mientras tanto) y `./mgmt-client -P 9` falla con mensaje claro.

---

## 3. Checklist de cierre de la Fase 0

- [ ] `make clean && make` sin warnings en macOS
- [ ] `make clean && make` sin warnings en Linux
- [ ] `make test` en verde (6 tests)
- [ ] `make debug` compila; Ctrl+C sin leaks
- [ ] `socks5d` escucha en v4, v6 y management; sale limpio con `SIGINT`/`SIGTERM`
- [ ] `mgmt-client` conecta al servidor
- [ ] `README.md` en UTF-8 y actualizado
- [ ] Atribución a la cátedra en `src/lib/` y en los archivos derivados de `docs/`
- [ ] Todo mergeado a `main` vía PR revisado
