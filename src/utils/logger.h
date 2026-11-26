#ifndef __logger_h_
#define __logger_h_
#include <stdio.h>
#include <stdlib.h>

/* 
*  Macros y funciones simples para log de errores.
*  EL log se hace en forma simple
*  Alternativa: usar syslog para un log mas completo. Ver sección 13.4 del libro de  Stevens
*/

typedef enum {DEBUG=0, INFO, ERROR, FATAL} LOG_LEVEL;

extern LOG_LEVEL current_level;

/**
*  Minimo nivel de log a registrar. Cualquier llamada a log con un nivel mayor a newLevel sera ignorada
**/
void setLogLevel(LOG_LEVEL newLevel);

char * levelDescription(LOG_LEVEL level);

// Debe ser una macro para poder obtener nombre y linea de archivo.
// FIX: renombrada de `log` a `LOG`: `log` choca con `log()` de <math.h> en
// cualquier archivo que incluya ambos headers (acuerdo de la Parte 4, fase 0).
// FIX: `fmt` pasó a formar parte de `__VA_ARGS__` (ya no hay parámetro `fmt`
// aparte) para no depender de `##__VA_ARGS__`, que es una extensión GNU y
// clang la marca con -pedantic en macOS (-Wgnu-zero-variadic-macro-arguments).
#define LOG(level, ...)   {if(level >= current_level) {\
	fprintf (stderr, "%s %s %s: %s:%d, ",__DATE__, __TIME__, levelDescription(level), __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr,"\n"); }\
	if ( level==FATAL) exit(1);}
#endif
