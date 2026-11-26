#include "config.h"

static struct config cfg;

void
config_init(const bool sniffer_enabled) {
    cfg.buffer_size       = CONFIG_DEFAULT_BUFFER_SIZE;
    cfg.connect_timeout_s = CONFIG_DEFAULT_CONNECT_TIMEOUT_S;
    cfg.idle_timeout_s    = CONFIG_DEFAULT_IDLE_TIMEOUT_S;
    cfg.sniffer_enabled   = sniffer_enabled;
    cfg.log_level         = current_level;
}

const struct config *
config_get(void) {
    return &cfg;
}

bool
config_set_buffer_size(const size_t bytes) {
    if (bytes < CONFIG_BUFFER_SIZE_MIN || bytes > CONFIG_BUFFER_SIZE_MAX) {
        return false;
    }
    cfg.buffer_size = bytes;
    return true;
}

bool
config_set_connect_timeout(const unsigned seconds) {
    if (seconds < CONFIG_CONNECT_TIMEOUT_MIN_S || seconds > CONFIG_CONNECT_TIMEOUT_MAX_S) {
        return false;
    }
    cfg.connect_timeout_s = seconds;
    return true;
}

bool
config_set_idle_timeout(const unsigned seconds) {
    if (seconds > CONFIG_IDLE_TIMEOUT_MAX_S) {
        return false;
    }
    cfg.idle_timeout_s = seconds;
    return true;
}

void
config_set_sniffer(const bool enabled) {
    cfg.sniffer_enabled = enabled;
}

bool
config_set_log_level(const LOG_LEVEL level) {
    if (level < DEBUG || level > FATAL) {
        return false;
    }
    setLogLevel(level);
    cfg.log_level = level;
    return true;
}
