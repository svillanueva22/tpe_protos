#ifndef CONNECTING_H_mN3pR8vK1qWxLz6TbYc5JeHd
#define CONNECTING_H_mN3pR8vK1qWxLz6TbYc5JeHd

#include "selector.h"

unsigned connecting_start(struct selector_key *key);
unsigned connecting_try_next(struct selector_key *key);
unsigned connecting_write_ready(struct selector_key *key);

#endif
