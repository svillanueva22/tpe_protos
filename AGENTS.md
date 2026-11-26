# AGENTS.md — TPE Protocolos de Comunicación 2026/2 (Proxy SOCKSv5)

Guía para agentes de código (Claude, Codex, Copilot, etc.) que trabajen en este repo.
Leela entera antes de tocar código. El enunciado oficial está en `docs/20262-r0.txt`;
si algo de este archivo lo contradice, **manda el enunciado**.

---

## 1. Qué estamos construyendo

Un servidor proxy **SOCKSv5** ([RFC 1928]) en **C11**, single-thread, con I/O
**no bloqueante multiplexada**, más un **protocolo propio de monitoreo/configuración**
y su **aplicación cliente**.

Binarios esperados (ya referenciados en el `clean` del Makefile):

| Binario       | Fuente         | Qué es                                              |
|---------------|----------------|-----------------------------------------------------|
| `socks5d`     | `src/`         | Servidor proxy SOCKSv5 + servidor de management     |
| `mgmt-client` | `src-client/`  | Cliente CLI del protocolo de monitoreo/configuración |

### Requerimientos funcionales (el servidor DEBE)

1. Atender **≥ 500 clientes concurrentes**.
2. Autenticación **usuario/contraseña** ([RFC 1929], método `0x02`).
3. `CONNECT` saliente TCP a **IPv4, IPv6 y FQDN** (que resuelva a cualquiera de los dos).
4. Ser robusto: si un FQDN resuelve a varias IPs y una falla, **probar la siguiente**.
5. Reportar fallos al cliente con **todos los códigos `REP`** que correspondan (ver §5.3).
6. Métricas (pueden ser volátiles): conexiones históricas, concurrentes, bytes
   transferidos, y otras que sumen (p. ej. fallos por tipo de `REP`, auth fallidas).
7. Gestionar usuarios y cambiar configuración **en runtime, sin reiniciar**.
8. **Access log**: un admin tiene que poder responder "¿quién se conectó a X y cuándo?".
9. **[Solo 2ª entrega]** Sniffer de credenciales tipo ettercap, al menos **POP3**.

### Requerimientos no funcionales

- **C11 estricto** (`-std=c11`), compila con `make` y un `Makefile` provisto.
- Sockets **no bloqueantes** y multiplexados: lecturas, escrituras **y `connect`/`accept`**
  por suscripción al selector. Esto es condición de aprobación.
- **Lecturas/escrituras parciales** manejadas correctamente. También condición de aprobación.
- Eficiencia: nunca cargar mensajes enteros en memoria; parsers incrementales byte a byte.
- Argumentos de línea de comando según **POSIX Utility Conventions** (getopt).
- Protocolo de management **documentado en detalle** (estilo RFC).
- Librerías de terceros: solo con licencia OSI, que no resuelvan el núcleo del TP y
  **aprobadas por la cátedra en el foro**. El código de la cátedra se puede usar atribuyéndolo.

---

## 2. Estado actual del repo (al 26/11/2026)

```
.
├── AGENTS.md            ← este archivo
├── Makefile             ← esqueleto, target `all` todavía no compila nada
├── README.md            ← placeholder; está en UTF-16, hay que reescribirlo en UTF-8
├── docs/
│   ├── 20262-r0.txt     ← ENUNCIADO
│   ├── 000{1,3..8}-*.patch  ← código base de la cátedra (Codagnone), ver §3
│   ├── args.c / args.h  ← parser de argumentos de la cátedra (getopt)
│   ├── main.c           ← main de ejemplo de la cátedra (selector + socket pasivo)
│   └── socks5nio.c      ← ESQUELETO INCOMPLETO de la cátedra (tiene "…" recortados)
├── references/          ← ejemplos de clase (echo TCP/UDP, select, fork, SIGIO). Solo lectura.
├── src/
│   ├── mgmt/            ← vacío (.gitkeep) → servidor del protocolo de management
│   ├── server/          ← vacío (.gitkeep) → main, args, config, métricas, logs
│   ├── socks5/          ← vacío (.gitkeep) → hello, auth, request, copy, sniffer
│   └── utils/           ← logger, util, tcpClientUtil, tcpServerUtil (de las clases)
└── src-client/          ← vacío (.gitkeep) → mgmt-client
```

Cosas a saber de lo que ya hay:

- `src/utils/tcpClientUtil.c` y `tcpServerUtil.c` son **bloqueantes** (`connect`,
  `accept`, `recv` en loop). Sirven para el **cliente** de management, **no** para el
  camino de datos del servidor. No usarlos en `socks5d` para el tráfico SOCKS.
- `tcpServerUtil.h` declara `void handleTCPEchoClient(int)` pero el `.c` devuelve `int`
  → no compila tal cual con `-Wall -Werror`. Arreglar o borrar el echo.
- `logger.h` define una macro `log(...)` que **choca con `log()` de `<math.h>`**. No
  incluir `math.h` en los mismos archivos, o renombrar la macro.
- `docs/args.c` dice "2025/1 -- Grupo X" y "AQUI VA LA LICENCIA": actualizar a 2026/2,
  número de grupo y licencia.
- `.vscode/` tiene paths de Windows de un compañero; no depender de eso.
- El remoto es `github.com/svillanueva22/tpe_protos`, rama `main`.

---

## 3. Código base de la cátedra (usar, no reinventar)

Los patches en `docs/` son de Juan F. Codagnone y están permitidos (atribuir). Aplicarlos
crea archivos en `src/` en la raíz:

```sh
git apply docs/0001-*.patch docs/0003-*.patch docs/0004-*.patch \
          docs/0005-*.patch docs/0006-*.patch docs/0007-*.patch docs/0008-*.patch
```

| Patch | Archivos                          | Para qué                                              |
|-------|-----------------------------------|-------------------------------------------------------|
| 0001  | `tests.h`                         | macro `N(x)`                                          |
| 0003  | `buffer.{c,h}` + test             | buffer con punteros de lectura/escritura (I/O parcial)|
| 0004  | `netutils.{c,h}` + test           | `sockaddr_to_human`, `sock_blocking_write`           |
| 0005  | `parser.{c,h}` + test             | motor de parsers por tabla de estados                 |
| 0006  | `parser_utils.{c,h}` + test       | parsers típicos (p. ej. string case-insensitive)      |
| 0007  | `selector.{c,h}` + test           | multiplexor (usa `pselect`) + jobs bloqueantes        |
| 0008  | `stm.{c,h}` + test                | máquina de estados atada a eventos del selector       |

(No hay patch 0002 en el repo; no hace falta.)

Después de aplicarlos, moverlos a una carpeta común (sugerido `src/lib/`) y ajustar includes.
**No reescribir el selector ni el stm** salvo bug concreto y documentado.

Detalles del selector que importan:

- Está implementado con **`select()` → límite `FD_SETSIZE` (1024)**. Cada conexión SOCKS
  usa **2 fds** (cliente + origen). 500 clientes ≈ 1000 fds + stdin/out/err + 2 sockets
  pasivos (SOCKS v4/v6) + management. Estamos al límite: no dejar fds abiertos de más,
  cerrar `stdin` como en `docs/main.c`, y documentar el máximo real en el informe.
- Las tareas bloqueantes (sobre todo **`getaddrinfo`**) se hacen en un **thread aparte** que
  al terminar llama a `selector_notify_block(selector, fd)`. El selector despierta con la
  señal configurada en `selector_init` (`SIGALRM`) y llama a `handle_block` en el thread
  principal. **Nunca llamar `getaddrinfo` en el thread del selector.** El thread solo
  resuelve y notifica; no toca estado compartido salvo su propio resultado.
- Linkear con `-pthread`.

`docs/socks5nio.c` y `docs/main.c` son guías, no código listo: faltan `hello.h`,
`request.h`, los estados de auth/request/connecting/copy y el manejo de errores. Además el
`on_hello_method` del esqueleto solo acepta "no auth": hay que aceptar **`0x02`
(USERNAME/PASSWORD)** y rechazar con `0xFF` si no viene.

---

## 4. Build

Objetivo: `make` en la raíz deja `socks5d` y `mgmt-client` listos.

Flags base:

```make
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L -pthread
LDFLAGS = -pthread
```

- `-std=c11` oculta `getaddrinfo`, `strdup`, `sigaction`, etc. → hace falta
  `_POSIX_C_SOURCE=200112L` (o `_DEFAULT_SOURCE`/`_GNU_SOURCE` si algo lo pide; documentarlo).
- `-fsanitize=address` está bien para desarrollo, pero **sacarlo para las pruebas de
  stress** (cambia memoria y fds). Sugerido: `make` normal y `make debug` con ASan/UBSan.
- Agregar target `make test` que compile y corra los `*_test.c` de la cátedra.
- Mantener el `clean` funcionando; los objetos van a `obj/` o `build/`, no a la raíz.
- Tiene que compilar **sin warnings en Linux** (la defensa probablemente sea en Linux). En
  macOS también debería compilar: ojo con `MSG_NOSIGNAL` (no existe en macOS → usar
  `SO_NOSIGPIPE` o ignorar `SIGPIPE` con `signal(SIGPIPE, SIG_IGN)`).

---

## 5. Arquitectura del servidor

### 5.1 Flujo general

```
main → parse_args → setup sockets pasivos (SOCKS + MGMT, IPv4 e IPv6, non-blocking)
     → selector_init / selector_new → register(passive, OP_READ)
     → loop selector_select() hasta SIGINT/SIGTERM → cleanup ordenado
```

Una conexión SOCKS = un `struct socks5` (alocación única, con pool de reuso y contador de
referencias, como en el esqueleto) + una `state_machine`.

### 5.2 Máquina de estados de una conexión SOCKS

```
HELLO_READ → HELLO_WRITE → AUTH_READ → AUTH_WRITE → REQUEST_READ
   → [REQUEST_RESOLV (thread getaddrinfo) →] REQUEST_CONNECTING → REQUEST_WRITE
   → COPY → DONE
cualquiera → ERROR
```

- Cada estado: `on_arrival` (setear intereses, init parser), `on_read_ready` /
  `on_write_ready` / `on_block_ready`, `on_departure` (liberar lo propio).
- Un estado **nunca** hace un loop de `recv`/`send` hasta completar: lee/escribe lo que
  haya, avanza el buffer, y vuelve al selector.
- `REQUEST_CONNECTING`: `socket` + `selector_fd_set_nio` + `connect` → `EINPROGRESS` →
  registrar el fd origen con `OP_WRITE` → al despertar, `getsockopt(SO_ERROR)`. Si falló,
  cerrar ese socket y **probar la siguiente `addrinfo`**; si no quedan, responder el
  `REP` correspondiente al último error.
- `COPY`: dos sentidos (cliente→origen, origen→cliente) con buffers propios. Interés de
  cada fd calculado según espacio libre / datos pendientes. Manejar **half-close**
  (`shutdown(SHUT_WR)` cuando un lado manda EOF) antes de cerrar todo.
- Hay que actualizar métricas (bytes) en `COPY` y registrar el access log al conocerse el
  resultado del `CONNECT`.

### 5.3 Códigos de respuesta (`REP`, RFC 1928 §6)

Usar "toda la potencia del protocolo". Mapeo sugerido:

| Situación                                         | REP    |
|---------------------------------------------------|--------|
| Éxito                                             | `0x00` |
| Error general / interno / sin memoria             | `0x01` |
| No permitido por reglas del servidor              | `0x02` |
| `ENETUNREACH`                                     | `0x03` |
| `EHOSTUNREACH`, `getaddrinfo` sin resultados      | `0x04` |
| `ECONNREFUSED`                                    | `0x05` |
| `ETIMEDOUT` (TTL expired)                         | `0x06` |
| `CMD` ≠ `CONNECT` (BIND/UDP ASSOCIATE)            | `0x07` |
| `ATYP` no soportado                               | `0x08` |

En la respuesta exitosa, `BND.ADDR`/`BND.PORT` = dirección local del socket al origen
(`getsockname`). Si el método no es aceptable → `0x05 0xFF` y cerrar. Auth RFC 1929:
respuesta `0x01 0x00` (ok) o `0x01 <≠0>` y cerrar.

### 5.4 Parsers

- Un parser por mensaje (hello, auth, request, protocolo de management, POP3), cada uno
  con `*_init`, `*_feed(byte)`/`*_consume(buffer)`, `*_is_done`, `*_has_error`, `*_close`.
- Son **incrementales**: tienen que funcionar si el mensaje llega de a 1 byte. Testearlo así.
- Validar longitudes (`ULEN`, `PLEN`, `DOMAIN len`, `NMETHODS`) y versiones (`0x05`, `0x01`).

### 5.5 Módulos sugeridos

```
src/lib/       buffer, selector, stm, parser, parser_utils, netutils  (cátedra)
src/utils/     logger, util                                           (clases)
src/server/    main.c, args.c, config.c (usuarios, timeouts, flags), metrics.c, accesslog.c
src/socks5/    socks5nio.c, hello.c, auth.c, request.c, copy.c, pop3_sniffer.c
src/mgmt/      mgmt.c (handlers/estado), mgmt_parser.c
src-client/    main.c, args.c, protocolo cliente
test/          tests unitarios + scripts de stress
```

Estado global (config, usuarios, métricas) en módulos con API explícita; nada de `extern`
sueltos repartidos.

---

## 6. Protocolo de management

**Lo diseña el grupo** y es parte central de la nota (se documenta detalladamente en el
informe, estilo RFC). Un agente puede proponer, pero las decisiones de diseño las toma el
equipo; dejarlas registradas en `docs/` con el porqué.

Mínimos a cubrir:

- Autenticación del administrador (no puede ser abierto a cualquiera).
- Consultar métricas (históricas, concurrentes, bytes, y extras).
- Alta/baja/listado de usuarios del proxy y cambio de contraseña.
- Cambiar configuración en runtime (p. ej. activar/desactivar sniffer, tamaño de buffers,
  timeouts, nivel de log).
- Consultar el access log / credenciales capturadas (o indicar dónde se escriben).
- Versionado y códigos de error claros.

Restricciones: el servidor de management corre **en el mismo selector** que SOCKS (no
bloquear), escucha por defecto en `127.0.0.1:8080`, y su parser también es incremental.
Elegir binario vs texto y TCP vs UDP es decisión del grupo; justificarla en el informe.

---

## 7. Argumentos de línea de comando

Base: `docs/args.c` (getopt, POSIX). Defaults actuales:

| Opción          | Significado                          | Default     |
|-----------------|--------------------------------------|-------------|
| `-h`            | ayuda                                |             |
| `-v`            | versión                              |             |
| `-l <addr>`     | dirección SOCKS                      | `0.0.0.0`   |
| `-p <port>`     | puerto SOCKS                         | `1080`      |
| `-L <addr>`     | dirección management                 | `127.0.0.1` |
| `-P <port>`     | puerto management                    | `8080`      |
| `-u user:pass`  | usuario del proxy (hasta 10)         |             |
| `-N`            | deshabilitar disectores (sniffer)    | habilitados |

El enunciado dice que los parámetros exactos se publican en otro documento: **si sale uno
nuevo, actualizar esta tabla**. El cliente `mgmt-client` tiene que usar opciones coherentes
con estas (mismo `-L`/`-P` para apuntar al servidor, `-h`, `-v`).

Por defecto escuchar también en IPv6 (`::`) además de IPv4, o documentar por qué no.

---

## 8. Logs

- **Access log** (req. 8): una línea por request, a archivo o stdout, con al menos fecha
  ISO-8601, usuario, IP:puerto del cliente, tipo de dirección destino, host/IP:puerto
  destino y `REP`. Pensado para grep: `grep 'example.com' access.log`.
- **Sniffer POP3** (req. 9, 2ª entrega): al ver tráfico hacia puerto 110, parsear de forma
  incremental `USER <x>` / `PASS <y>` (y que el `+OK` confirme) sin alterar el flujo.
  Loguear fecha, usuario del proxy, destino, usuario y password capturados. Respetar `-N`
  y el toggle en runtime.
- El logger de debug (`logger.h`) va a `stderr`. Las escrituras de logs **no** pueden
  bloquear el selector en el camino caliente: bufferizar o escribir a archivo con cuidado.

---

## 9. Testing

Herramientas típicas:

```sh
# Smoke test
./socks5d -u user:pass &
curl -v --socks5-hostname user:pass@127.0.0.1:1080 http://example.com/
curl -v --socks5 user:pass@127.0.0.1:1080 http://[::1]:8000/      # IPv6
curl -v --socks5-hostname user:pass@127.0.0.1:1080 http://localhost:9/  # REP 0x05

# Concurrencia (subir el límite de fds primero; en macOS el default es 256)
ulimit -n 4096
```

- Probar mensajes partidos de a 1 byte (p. ej. con un cliente en Python que envíe con
  `sleep` entre bytes) para verificar parsers y I/O parcial.
- Probar FQDN con varias IPs donde la primera no responde (p. ej. `/etc/hosts` con una IP
  muerta + una viva, o un dominio con AAAA inalcanzable).
- **Stress obligatorio para el informe**: máximo de conexiones simultáneas y degradación del
  throughput. Guardar scripts en `test/` y resultados (tablas/gráficos) para el informe.
  Compilar **sin ASan** para estas mediciones.
- Correr con `valgrind --leak-check=full` (Linux) y terminar con `SIGINT` para verificar
  que no haya leaks ni fds colgados.

---

## 10. Entregables (no olvidar)

El tarball entregado es un `git clone` del repo, así que **todo tiene que estar commiteado**:

- Informe (PDF o texto UTF-8) con las 11 secciones **en este orden**: índice; descripción
  de protocolos y aplicaciones; problemas encontrados; limitaciones; posibles extensiones;
  conclusiones; ejemplos de prueba; guía de instalación; instrucciones de configuración;
  ejemplos de configuración y monitoreo; documento de diseño.
- Código fuente + Makefile.
- `README.md` en la raíz (UTF-8) con: ubicación de los materiales, cómo compilar, dónde
  quedan los binarios, cómo ejecutarlos y sus opciones.

---

## 11. Reglas para agentes

1. **No romper las condiciones de aprobación**: nada de `recv`/`send`/`connect`/`accept`/
   `getaddrinfo` bloqueantes en el thread del selector; siempre manejar `EAGAIN`/
   `EWOULDBLOCK`/`EINTR` y escrituras parciales.
2. **C11 + POSIX**, sin extensiones GNU salvo que se justifiquen. Sin librerías externas
   sin aprobación de la cátedra (avisar al humano antes de agregar cualquiera).
3. Antes de terminar una tarea: `make clean && make` sin warnings y `make test` en verde.
4. Cambios chicos y enfocados. **No commitear ni pushear** salvo que el humano lo pida; el
   historial de git se entrega y cada integrante tiene que poder defender su parte.
5. Explicar en el mensaje final qué se cambió y por qué, de forma que un integrante del
   grupo pueda defenderlo en el coloquio. Si tomaste una decisión de diseño, decilo.
6. No modificar `references/` ni el enunciado. Los patches de la cátedra se aplican, no se
   editan in situ; si se corrige algo del código de la cátedra, comentar `// FIX:` con el motivo.
7. Atribuir el código de la cátedra en el header de cada archivo
   (`Basado en código de Juan F. Codagnone — Protocolos de Comunicación, ITBA`).
8. Liberar todo en `on_departure`/`close`: cada `malloc` y cada fd tiene un dueño claro.
9. Comentarios y mensajes de log en español (consistente con el código de la cátedra);
   identificadores en inglés o español, pero consistentes dentro de cada módulo.
10. Ante ambigüedad en el enunciado, **no inventar en silencio**: proponer opciones con
    pros/contras y dejar la decisión al equipo (el enunciado lo pide explícitamente).

---

## Referencias

- [RFC 1928] SOCKS Protocol Version 5 — https://www.rfc-editor.org/rfc/rfc1928
- [RFC 1929] Username/Password Authentication for SOCKS V5 — https://www.rfc-editor.org/rfc/rfc1929
- [RFC 1939] Post Office Protocol v3 (para el sniffer) — https://www.rfc-editor.org/rfc/rfc1939
- POSIX Utility Conventions — https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap12.html
- Enunciado: `docs/20262-r0.txt`

[RFC 1928]: https://www.rfc-editor.org/rfc/rfc1928
[RFC 1929]: https://www.rfc-editor.org/rfc/rfc1929
