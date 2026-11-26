# Decisiones de diseño — Parte C

Registro de decisiones de la Parte C (estado del servidor, logs y sniffer), con el porqué,
para el informe y para la defensa. Una decisión por entrada. Si una se revisa, se agrega
una nota al final de esa entrada en vez de borrarla.

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

## Pendientes de decidir (Parte C)

Se van charlando de a una; al decidir cada una se pasa a una entrada propia arriba.

- **Qué más registra el access log** para el caso de la queja externa: IP resuelta del
  destino (la completa la Parte B), intentos de auth fallidos como línea propia, línea de
  cierre con bytes y duración.
- **Firma de `accesslog_write`**: hoy recibe primitivos (usuario, cliente, tipo de
  dirección, destino como texto, puerto, REP) para no depender de `request.h` (Parte A).
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
