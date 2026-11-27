#ifndef COPY_H_vR5kT9mWn2QxLp7YcBz4JeHd
#define COPY_H_vR5kT9mWn2QxLp7YcBz4JeHd

#include "selector.h"

void     copy_arrival(unsigned state, struct selector_key *key);
unsigned copy_read_ready(struct selector_key *key);
unsigned copy_write_ready(struct selector_key *key);

#endif
