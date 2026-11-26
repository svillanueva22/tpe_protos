#include <string.h>

#include "metrics.h"

static struct metrics_snapshot m;

void
metrics_init(void) {
    memset(&m, 0, sizeof(m));
}

void
metrics_conn_open(void) {
    m.conn_total++;
    m.conn_current++;
    if (m.conn_current > m.conn_peak) {
        m.conn_peak = m.conn_current;
    }
}

void
metrics_conn_close(void) {
    // un close de más no debe dar la vuelta a 2^64
    if (m.conn_current > 0) {
        m.conn_current--;
    }
}

void
metrics_bytes(const metrics_dir dir, const size_t n) {
    if (dir == METRICS_C2O) {
        m.bytes_c2o += n;
    } else if (dir == METRICS_O2C) {
        m.bytes_o2c += n;
    }
}

void
metrics_rep(const uint8_t rep) {
    if (rep <= METRICS_REP_MAX) {
        m.rep[rep]++;
    }
}

void
metrics_auth_ok(void) {
    m.auth_ok++;
}

void
metrics_auth_fail(void) {
    m.auth_fail++;
}

void
metrics_get(struct metrics_snapshot *out) {
    *out = m;
}
