# Decisiones de diseño — Parte C

Registro de decisiones de la Parte C (estado del servidor, logs y sniffer), con el porqué,
para el informe y para la defensa. Una decisión por entrada. Si una se revisa, se agrega
una nota al final de esa entrada en vez de borrarla.

## Resumen de lo decidido

| # | Decisión | Elegido | Alternativas descartadas |
|---|----------|---------|--------------------------|
| D1 | E/S del access log | Archivo con buffer, un flush por vuelta del selector | Cola + fd no bloqueante; thread de log (queda como mejora pendiente) |
| D2 | Qué IP registra | `ip=` con la IP a la que realmente se conectó, además de `dest=` | Solo el destino pedido |
| D3 | Intentos de auth fallidos | Línea `event=auth_fail` en el mismo access log | Solo el contador; archivo aparte |
| — | Línea de cierre con bytes y duración | No se hace (no la pide el req. 8) | — |

---

## D1 — Cómo escribe el access log sin frenar al selector

**Fecha:** 2026-11-26 · **Estado:** decidida (revisable, ver "Mejora pendiente")

**Contexto.** La consigna hace condición de aprobación que la E/S sea no bloqueante
(`docs/20262-r0.txt`, sección 3), y `AGENTS.md` §8 pide que el log no bloquee el selector.
El access log (requerimiento 8) escribe una línea por request en el thread del selector.

**Opciones consideradas.**

| Opción | Qué hace | Problema |
|--------|----------|----------|
| A. Archivo + buffer | Buffer de stdio; un `write()` por vuelta del selector | El `write()` sigue siendo bloqueante en teoría |
| B. Cola + fd no bloqueante | Cola en memoria que vacía un fd `O_NONBLOCK` registrado con `OP_WRITE` | `O_NONBLOCK` no hace nada sobre archivos regulares y `select()` siempre los da por listos: solo sirve con pipes, sockets o terminal. Además hay que resolver la cola llena |
| C. Thread de log | Un thread escribe de una cola compartida (patrón de `selector_notify_block`) | Cola compartida con mutex, política de cola llena, cierre ordenado (`join`), más difícil de testear y de depurar con sanitizers |

**Decisión: A.** Las líneas se acumulan en un buffer de stdio de 16 KiB y `main` llama a
`accesslog_flush()` una vez por vuelta del selector. Por defecto se escribe en un archivo
(`access.log`), no en stdout.

**Por qué.**
- Escribir a un archivo regular casi nunca bloquea: el kernel lo guarda en memoria y lo
  baja a disco después. El riesgo real está en stdout si va a un pipe o una terminal, y por
  eso el default es un archivo.
- La opción B no aporta nada sobre un archivo regular, y perder líneas con la cola llena es
  peor que bloquear un instante en un log de auditoría.
- La opción C es la única que garantiza no bloquear, pero agrega sincronización entre
  threads en un servidor que hoy es de un solo thread.
- La API pública (`accesslog_write`) es idéntica en las tres opciones, así que cambiar de
  opción después no afecta a las Partes A, B ni D.

**Consecuencias / limitaciones (van al informe, sección "Limitaciones").**
- El `write()` a disco sigue siendo bloqueante: un disco o un NFS colgado frenaría al
  selector. Se absorbe en el page cache, pero no está garantizado.
- Si el proceso muere sin cerrar limpio (`kill -9`), se pierde lo que quedó en el buffer: a
  lo sumo una vuelta del selector (el selector despierta al menos cada 10 s por timeout).
  Con `SIGINT`/`SIGTERM` se vuelca todo en `accesslog_close`.
- Si el buffer se llena en medio de una vuelta, stdio hace un `write()` extra.

**Mejora pendiente (si sobra tiempo antes de la entrega).** Pasar a la opción C: un thread
que solo escribe el log, sin necesidad de avisarle nada al selector (más simple que el de
`getaddrinfo`). Cambia solo `src/server/accesslog.c`. Es la única forma de poder afirmar
"el selector nunca se bloquea por el log".

---

## D2 — El access log registra la IP a la que realmente se conectó

**Fecha:** 2026-11-26 · **Estado:** decidida

**Contexto.** El requerimiento 8 pide que el administrador pueda saber quién se conectó a
cierto sitio y cuándo ante una queja externa. Hasta ahora la línea solo tenía el destino
pedido por el cliente (por ejemplo `example.com`).

**Problema.** La queja la hace el dueño del sitio, que ve en su log la IP del proxy y la
hora, no el nombre que usó el cliente. Si trae la IP del servidor, un `grep` por IP no
encuentra nada. Además un nombre puede resolver a IPs distintas según el momento o el
balanceo, y después el administrador ya no puede reconstruirlo consultando el DNS.

**Decisión.** Cada línea lleva `ip=<IP>` además de `dest=<lo pedido>:<puerto>`:

```
2026-11-26T22:42:31Z user=alice client=10.0.0.5:51234 atyp=fqdn dest=example.com:80 ip=93.184.216.34 rep=0x00
```

- Si hubo que probar varias IPs (requerimiento 4), se loguea la que funcionó; si fallaron
  todas, la última que se intentó.
- Si no hubo ninguna (`getaddrinfo` sin resultados, request rechazado antes de conectar):
  `ip=-`.
- `accesslog_write` recibe un `struct sockaddr *dest_ip` (puede ser `NULL`), no un string:
  el formato (incluida la IPv6) lo resuelve el módulo y la Parte B no formatea nada. El
  puerto de esa estructura se ignora; el puerto ya está en `dest=`.

**Limitación (va al informe).** Una IP no identifica un sitio: con CDNs o hosting
compartido muchos sitios comparten IP. Por eso se guardan juntos el nombre, la IP y la hora.

**Quién lo completa.** La Parte B, que es quien conoce la IP a la que conectó. Ver el
comentario de `accesslog_write` en `src/server/accesslog.h`.

---

## D3 — Los intentos de autenticación fallidos quedan en el access log

**Fecha:** 2026-11-26 · **Estado:** decidida

**Contexto.** Hasta ahora un fallo de RFC 1929 solo sumaba `metrics_auth_fail`, un contador
global: sin quién lo intentó, desde dónde ni cuándo. El requerimiento 8 pide entender los
accesos de cada usuario.

**Opciones.** (A) dejar solo el contador; (B) una línea en el mismo access log; (C) un
archivo aparte (`auth.log`).

**Decisión: B.** Una línea por intento fallido, con un campo `event=` que distingue el tipo
de línea. Las líneas de requests pasan a llevar `event=request`:

```
2026-11-26T22:42:31Z event=request user=alice client=10.0.0.5:51234 atyp=fqdn dest=example.com:80 ip=93.184.216.34 rep=0x00
2026-11-26T22:42:45Z event=auth_fail user=admin client=203.0.113.5:40022
```

- `accesslog_auth_fail(client, user, ulen)` lo llama la Parte A al rechazar las credenciales
  (el mismo par `user, ulen` que le pasa a `users_check`). La contraseña no se recibe ni se
  loguea nunca.
- Un `grep user=bob` muestra todo lo que ese usuario hizo o intentó: éxitos y fallos juntos.
  `grep event=auth_fail` lista solo los rechazos.
- `metrics_auth_fail` se sigue contando.
- C descartada: dos archivos que rotar y buscar, sin ganar nada sobre B.

**Limitaciones (van al informe).**
1. **Un anónimo controla el volumen del log.** Hasta ahora cada línea requería haberse
   autenticado; con D3 cualquiera que llegue al puerto genera líneas. No hay límite de tasa
   (sería excesivo para este TP). El tamaño de cada línea sí está acotado: el usuario se
   trunca a 255 bytes.
2. **`user=` puede contener una contraseña**, si alguien la escribió en el campo de usuario.
   Se trunca y se limpia, pero ese dato queda en el archivo.

---

## Pendientes de decidir (Parte C)

Se van charlando de a una; al decidir cada una se pasa a una entrada propia arriba.

- **Línea de cierre con bytes y duración**: descartada por ahora (no la pide el
  requerimiento 8 y obliga a la Parte B a llevar contadores por conexión). Si se quiere
  consumo por usuario, es más limpio como métrica por usuario.
- **Firma de `accesslog_write`**: recibe primitivos (usuario, cliente, tipo de dirección,
  destino como texto, puerto, IP destino, REP) para no depender de `request.h` (Parte A).
  La alternativa del plan es recibir el `struct request`.
- **Consulta del access log desde management** (Parte D): solo informar la ruta del archivo
  o guardar las últimas N líneas en memoria y devolverlas por el protocolo.
- **Zona horaria del access log**: hoy UTC (`...Z`). Alternativa: hora local con offset.
- **Valores por defecto de `config`**: buffer 4096 bytes, timeout de connect 10 s, timeout de
  inactividad 300 s.
- **Tope de usuarios en runtime**: hoy `USERS_MAX = 64` (la línea de comandos admite 10).
- **Opción de línea de comandos** para elegir la ruta del access log (hoy fija en
  `access.log`) y para las credenciales del administrador de management (esta última
  depende del diseño de la Parte D).
