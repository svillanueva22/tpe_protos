# Plan del TP — reparto en 4 partes

Objetivo: terminar el TP completo (requerimientos 1–9 de `docs/20262-r0.txt`, informe,
README y pruebas de stress) repartido en **4 partes equitativas** que cada integrante
pueda hacer **de forma independiente**.

Ante cualquier duda mandan el enunciado y `AGENTS.md`.

---

## 0. Prerrequisitos e hitos

- **Fase 0 cerrada** (checklist de `docs/fase0-reparto.md`). Al 26/11 (`ac27cb7`) están
  mergeadas las 4 partes y `socks5d`/`mgmt-client` compilan, pero falta:
  - `src/utils/logger.h`, `src-client/args.h` y `src-client/main.c` no terminan en
    newline → clang tira `-Wnewline-eof` (el checklist pide cero warnings).
  - Correr `make test` en Linux y en una Mac con Check instalado
    (`brew install check pkg-config`) y confirmar los 6 tests en verde.
- **Entrega 1:** requerimientos 1–8, informe, RFC del protocolo de management. Fecha: `____`
- **Entrega 2:** sniffer POP3 (req. 9) y correcciones de la entrega 1. Fecha: `____`

> **El sniffer POP3 NO entra en la Entrega 1.** El enunciado marca el req. 9 como
> "[SOLO PARA SEGUNDA ENTREGA]" (`docs/20262-r0.txt`, sección 1). En la Entrega 1 nadie
> implementa ni cablea el sniffer: las Partes A y B lo ignoran. Solo quedó un **stub** con la
> API definitiva (`src/socks5/pop3_sniffer.{h,c}`, de C) para que en la Entrega 2 alcance con
> implementar la lógica y sumar dos llamadas en `COPY`. Todo lo marcado **[Entrega 2]** más
> abajo queda para esa entrega.

---

## 1. Contratos comunes (se acuerdan juntos, ~1 h, antes de dividirse)

Las partes solo se tocan a través de estos headers. Cada dueño sube primero una
**implementación stub** (valores fijos, sin lógica) para que todos compilen y prueben sin
esperar a nadie. Cambiar una firma = avisar al grupo antes.

| Header | Dueño | Lo usan | API mínima |
|--------|-------|---------|------------|
| `src/socks5/socks5nio.h` | A | B, `main.c` | `enum socks_v5state` completo, `struct socks5` (union de estados), `socksv5_passive_accept`, `socksv5_pool_destroy` |
| `src/socks5/request.h` | A | B, C | `struct request { cmd, atyp, dest (v4/v6/fqdn), dest_port }` y `enum socks_reply` (tabla `AGENTS.md` §5.3) |
| `src/server/users.h` | C | A, D | `users_check(user, ulen, pass, plen)` (con longitudes, sin `'\0'`), `users_add`, `users_del`, `users_set_pass`, `users_count` + `users_name_at(i)` para listar |
| `src/server/metrics.h` | C | A, B, D | `metrics_conn_open/close`, `metrics_bytes(dir, n)`, `metrics_rep`, `metrics_auth_ok/fail`, `metrics_get` |
| `src/server/accesslog.h` | C | A, B, D | `accesslog_write(user, client, atyp, dest, dest_port, dest_ip, rep)` (B): `dest` es lo pedido (texto) y `dest_ip` la IP a la que conectó B (o `NULL`). `accesslog_auth_fail(client, user, ulen)` (A) al rechazar credenciales. `main` hace el flush. Ver `docs/decisiones-parte-c.md` (D1, D2, D3) |
| `src/server/config.h` | C | todos | `config_get`, `config_set_*` (buffer size, timeouts, sniffer on/off, log level) |
| `src/socks5/pop3_sniffer.h` | C | B | **[Entrega 2]** stub con la API definitiva: `pop3_sniffer_applies`, `pop3_sniffer_init`, `pop3_sniffer_feed(sniffer, ptr, n, dir)`. No se usa en la Entrega 1 |
| `src/mgmt/mgmt.h` | D | `main.c` | `mgmt_passive_accept`, `mgmt_pool_destroy` |

`src/server/main.c` y `src/server/args.c` son de **C**. Hoy `listen_on` registra el mismo
`passive_accept` provisorio para SOCKS y management; C lo cambia para que reciba el handler,
y registra `socksv5_passive_accept` (A) y `mgmt_passive_accept` (D). A y D no editan `main.c`.

Credenciales del administrador de management: D define cómo se autentica; C agrega la
opción de línea de comandos al servidor (las opciones exactas las publica la cátedra en otro
documento; si sale, se adapta) y D la del cliente.

Tests nuevos: cada parte los pone en `test/<modulo>_test.c` con Check, igual que los de
`src/lib/`. C agrega al `Makefile` una regla genérica para `test/*_test.c` en la primera semana.

Estados de la conexión SOCKS (A define el enum, cada uno implementa los suyos):

```text
HELLO_READ → HELLO_WRITE → AUTH_READ → AUTH_WRITE → REQUEST_READ      (A)
  → REQUEST_RESOLV → REQUEST_CONNECTING                                (B)
  → REQUEST_WRITE                                                      (A)
  → COPY → DONE                                                        (B)
cualquiera → ERROR
```

Reglas: estado global solo detrás de estos headers, nada de `extern` sueltos, log con
`LOG(level, ...)`, el `Makefile` lo mantiene C (los demás le piden agregar fuentes),
cada PR lo revisa otro integrante.

---

## 2. Las 4 partes

### Parte A — Negociación SOCKS5

**Archivos:** `src/socks5/socks5nio.{c,h}`, `hello.{c,h}`, `auth.{c,h}`, `request.{c,h}`, `test/`

1. Completar `socks5nio.c` desde el esqueleto: pool con refcount, `socksv5_passive_accept`,
   tabla `client_statbl`, handlers read/write/block/close. C lo engancha en `main.c` en
   lugar del `passive_accept` provisorio.
2. Parser incremental **hello** (usando `src/lib/parser.h` o a mano): acepta `0x02`;
   si no viene, responde `0x05 0xFF` y cierra.
3. Parser incremental **auth RFC 1929** + respuesta `0x01 0x00` / `0x01 0x01`. Valida con
   `users_check`; si las rechaza, cuenta el fallo con `metrics_auth_fail` y lo registra con
   `accesslog_auth_fail(client, user, ulen)` (decisión D3); si las acepta, `metrics_auth_ok`.
4. Parser incremental **request**: `CONNECT` con ATYP IPv4, IPv6 y FQDN. `REP 0x07` para
   BIND/UDP, `REP 0x08` para ATYP desconocido.
5. `REQUEST_WRITE`: arma la respuesta con `BND.ADDR`/`BND.PORT` (`getsockname` del fd que
   deja B) y pasa a `COPY`; en error, el `REP` que dejó B y cierra.
6. Tests unitarios de los 3 parsers alimentados de a 1 byte, con mensajes inválidos.

**Prueba aislada:** mientras no esté B, después de `REQUEST_READ` contestar `REP 0x01` y cerrar.
**Informe:** descripción de SOCKS5 + RFC 1929 y diseño de los parsers.
**Listo cuando:** `curl --socks5-hostname user:pass@127.0.0.1:1080 ...` recibe la respuesta
al request, credenciales erróneas se rechazan y los tests de parsers pasan.

### Parte B — Conexión al origen y relay

**Archivos:** `src/socks5/resolv.{c,h}`, `connecting.{c,h}`, `copy.{c,h}`, `test/stress/`

0. `src/lib/selector.c:585`: `selector_fd_set_nio` lee los flags con `F_GETFD` en vez de
   `F_GETFL`, así que al poner `O_NONBLOCK` pisa los flags de estado del fd. Corregirlo con
   `// FIX:` antes de usarlo en el connect.
1. `REQUEST_RESOLV`: `getaddrinfo` en un thread aparte que termina con
   `selector_notify_block`; el resultado se procesa en `on_block_ready`. IPv4/IPv6 literales
   no pasan por el thread.
2. `REQUEST_CONNECTING`: `socket` + `selector_fd_set_nio` + `connect` → `EINPROGRESS` →
   `OP_WRITE` → `getsockopt(SO_ERROR)`. Si falla, **siguiente `addrinfo`**; si no quedan,
   errno → `REP` (`ENETUNREACH` 0x03, `EHOSTUNREACH` 0x04, `ECONNREFUSED` 0x05, `ETIMEDOUT` 0x06).
3. `COPY`: un buffer por sentido, intereses según espacio libre/datos pendientes,
   half-close con `shutdown(SHUT_WR)`, cierre cuando ambos lados terminaron.
4. Ganchos: `metrics_bytes` en cada transferencia y `accesslog_write` al conocer el resultado
   del connect (con la IP a la que se conectó, ver D2). **[Entrega 2]** `pop3_sniffer_feed`
   en `COPY` si el destino es el puerto 110: no se hace en la Entrega 1.
5. Timeout de connect e inactividad, con valor tomado de `config`.
6. **Stress:** scripts en `test/stress/` para máximo de conexiones simultáneas y throughput
   vs. cantidad de clientes; compilado sin ASan; tablas/gráficos y límite real de `FD_SETSIZE`.

**Prueba aislada:** test que crea un `struct socks5` con un `struct request` fijo y entra
directo en `REQUEST_RESOLV`.
**Informe:** arquitectura no bloqueante, pruebas de stress, limitaciones.
**Listo cuando:** pasan los casos de `AGENTS.md` §9 (IPv4, IPv6, FQDN, FQDN con una IP
muerta, `REP 0x05`) y el servidor sostiene 500 clientes simultáneos.

### Parte C — Estado del servidor, logs y sniffer

**Archivos:** `src/server/users.c`, `metrics.c`, `accesslog.c`, `config.c`,
`src/socks5/pop3_sniffer.c`, todos los headers de C, `src/server/main.c`, `src/server/args.c`,
`Makefile`, `README.md`, `docs/informe`

1. **Día 1:** subir los headers de la sección 1 que le tocan, con stubs, y la regla de
   `Makefile` para `test/*_test.c`.
2. `users.c`: carga inicial desde `args.users` (ya validados por `args.c`), alta/baja/cambio
   de contraseña en runtime, límites RFC 1929.
3. `metrics.c`: históricas, concurrentes, bytes, fallos por `REP`, auth fallidas.
4. `config.c`: valores en runtime (buffer size, timeouts, sniffer on/off, nivel de log).
5. `accesslog.c`: una línea por request (fecha ISO-8601, usuario, cliente, destino, `REP`),
   apta para `grep`, sin bloquear el selector.
6. **Sniffer POP3 (Entrega 2):** parser incremental de `USER`/`PASS` confirmado por `+OK`,
   sin alterar el flujo, respeta `-N` y el toggle de `config`.
7. `main.c`: `listen_on` con handler por parámetro, enganchar `socksv5_passive_accept` y
   `mgmt_passive_accept`, inicializar users/metrics/config/accesslog y liberarlos al salir.
   Opción de credenciales del admin en `args.c`.
8. Mantener el `Makefile` y escribir el `README.md` final.
9. **Ensamblar el informe** (11 secciones en el orden del enunciado) con lo que escribe cada uno.

**Prueba aislada:** tests unitarios de cada módulo; **[Entrega 2]** sniffer alimentado de a 1 byte.
**Informe:** logs, guía de instalación, instrucciones de configuración, índice y conclusiones;
**[Entrega 2]** sniffer.
**Listo cuando:** tests en verde y `grep example.com access.log` responde quién y cuándo;
**[Entrega 2]** el sniffer captura credenciales de una sesión POP3 real.

### Parte D — Protocolo de management y cliente

**Archivos:** `src/mgmt/`, `src-client/`, `docs/mgmt-rfc.txt`

1. **Diseño** (propuesta al grupo, decisión registrada en `docs/` con el porqué): binario
   vs. texto, TCP, autenticación del admin, versionado y códigos de error.
2. Escribir el **RFC del protocolo** en `docs/mgmt-rfc.txt`.
3. Servidor en el mismo selector: accept no bloqueante, parser incremental, respuestas con
   escrituras parciales.
4. Comandos: métricas, usuarios (alta/baja/listado/cambio de contraseña), configuración en
   runtime y consulta del access log. **[Entrega 2]** consulta de las credenciales capturadas.
5. `mgmt-client`: ya tiene `-h`, `-v`, `-L`, `-P` y conecta (`src-client/args.c`,
   `src-client/main.c`, en el `TODO Fase 3`). Falta la opción de credenciales del admin,
   un subcomando por operación, salida legible y códigos de salida claros.
6. Tests del parser de a 1 byte y un script con ejemplos de configuración y monitoreo.

**Prueba aislada:** contra los stubs de C (métricas fijas, usuarios en memoria).
**Informe:** descripción del protocolo de management, ejemplos de configuración y monitoreo.
**Listo cuando:** `mgmt-client` consulta métricas, da de alta un usuario y ese usuario puede
usar el proxy sin reiniciar el servidor.

---

## 3. Resumen de carga

| Parte | Código principal | Tests | Secciones del informe |
|-------|------------------|-------|-----------------------|
| A | máquina de estados + 3 parsers | parsers de a 1 byte | protocolos SOCKS/RFC 1929, diseño de parsers |
| B | resolución, connect, relay | casos de conexión + stress | arquitectura, stress, limitaciones |
| C | users, metrics, config, accesslog, sniffer, `main.c` | unitarios + POP3 | logs/sniffer, instalación, configuración, ensamblado |
| D | servidor + cliente de management | parser + script de ejemplos | RFC de management, ejemplos de configuración y monitoreo |

"Problemas encontrados" y "posibles extensiones" las escribe cada uno sobre su parte.

---

## 4. Integración y cierre

Orden de merge: **C (stubs) → A → B → D → C (implementaciones reales y sniffer)**.

Checklist de entrega:

- [ ] `make clean && make` sin warnings en Linux; `make test` en verde
- [ ] `valgrind --leak-check=full` sin leaks al cortar con `SIGINT`
- [ ] Casos de `AGENTS.md` §9 y 500 clientes simultáneos
- [ ] Informe con las 11 secciones en orden
- [ ] `docs/mgmt-rfc.txt` completo
- [ ] `README.md` con ubicación de materiales, build, artefactos y ejecución
- [ ] Tarball generado desde un `git clone` (todo commiteado)
- [ ] Ensayo de la demo: cada integrante puede explicar su parte
