.PHONY: all clean test debug

CC      = gcc
# EXTRA_CFLAGS lo usa `make debug` para agregar los sanitizers (ver más abajo).
EXTRA_CFLAGS =
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L -pthread $(EXTRA_CFLAGS)
LDFLAGS = -pthread $(EXTRA_CFLAGS)
BUILD   = build
# SUFFIX vale "-debug" en `make debug`, para no pisar los binarios normales.
SUFFIX  =

INCLUDES = -Isrc/lib -Isrc/server -Isrc/socks5 -Isrc/mgmt -Isrc/utils

# ── Fuentes ──────────────────────────────────────────────────────────

LIB_SRCS = \
    src/lib/buffer.c       \
    src/lib/netutils.c     \
    src/lib/parser.c       \
    src/lib/parser_utils.c \
    src/lib/selector.c     \
    src/lib/stm.c

SERVER_SRCS = \
    src/server/main.c      \
    src/server/args.c      \
    src/server/users.c     \
    src/server/metrics.c   \
    src/server/accesslog.c

SOCKS5_SRCS = \
    src/socks5/socks5nio.c   \
    src/socks5/hello.c       \
    src/socks5/auth.c        \
    src/socks5/request.c     \
    src/socks5/resolv.c      \
    src/socks5/connecting.c  \
    src/socks5/copy.c

UTILS_SRCS = \
    src/utils/logger.c \
    src/utils/util.c

ALL_SRCS = $(LIB_SRCS) $(SERVER_SRCS) $(SOCKS5_SRCS) $(UTILS_SRCS)
ALL_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(ALL_SRCS))

# Cliente de management: todo lo que haya en src-client/ + utils bloqueantes de clase.
CLIENT_SRCS       = $(wildcard src-client/*.c)
CLIENT_OBJS       = $(patsubst src-client/%.c,$(BUILD)/client/%.o,$(CLIENT_SRCS))
CLIENT_UTILS_SRCS = \
    src/utils/logger.c        \
    src/utils/util.c          \
    src/utils/tcpClientUtil.c
CLIENT_UTILS_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(CLIENT_UTILS_SRCS))

# ── Targets principales ───────────────────────────────────────────────

all: socks5d$(SUFFIX) mgmt-client$(SUFFIX)

socks5d$(SUFFIX): $(ALL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

ifeq ($(strip $(CLIENT_SRCS)),)
mgmt-client$(SUFFIX):
	@echo "mgmt-client: pendiente (src-client/ todavía no tiene fuentes)"
else
mgmt-client$(SUFFIX): $(CLIENT_OBJS) $(CLIENT_UTILS_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
endif

# `make debug`: igual que `all` pero con ASan/UBSan. Usa otro directorio de objetos y
# otros nombres de binario (socks5d-debug, mgmt-client-debug) para no mezclarlos con
# los normales: las pruebas de stress se hacen SIEMPRE con el build sin sanitizers.
debug:
	$(MAKE) BUILD=$(BUILD)/debug SUFFIX=-debug \
	    EXTRA_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" all

# ── Compilación de objetos ────────────────────────────────────────────
# -MMD -MP genera los .d para que un cambio en un .h recompile lo que lo incluye.

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c -o $@ $<

$(BUILD)/client/%.o: src-client/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -Isrc-client -MMD -MP -c -o $@ $<

-include $(ALL_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d) $(CLIENT_UTILS_OBJS:.o=.d)

# ── Tests (requiere Check: apt install check pkg-config / brew install check) ─────

# pkg-config resuelve rutas y librerías (en macOS con Homebrew no están en el default).
CHECK_CFLAGS := $(shell pkg-config --cflags check 2>/dev/null)
CHECK_LIBS   := $(shell pkg-config --libs check 2>/dev/null)
ifeq ($(strip $(CHECK_LIBS)),)
  # Sin pkg-config: valores por defecto según el sistema.
  ifeq ($(shell uname -s),Darwin)
    CHECK_LIBS = -lcheck -lm -lpthread
  else
    CHECK_LIBS = -lcheck -lsubunit -lrt -lm -lpthread
  endif
endif

TEST_CC = $(CC) $(CFLAGS) $(CHECK_CFLAGS) $(INCLUDES)

TESTS = $(BUILD)/test_buffer $(BUILD)/test_netutils $(BUILD)/test_parser \
        $(BUILD)/test_parser_utils $(BUILD)/test_selector $(BUILD)/test_stm \
        $(BUILD)/test_hello $(BUILD)/test_auth $(BUILD)/test_request

# Si un test falla, `make test` termina con error.
test: $(TESTS)
	@for t in $^; do echo "── $$t ──"; $$t || exit 1; done

# buffer_test y selector_test incluyen el .c directamente → compilar solos
$(BUILD)/test_buffer: src/lib/buffer_test.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $< $(CHECK_LIBS)

$(BUILD)/test_selector: src/lib/selector_test.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $< $(CHECK_LIBS)

# netutils.c usa buffer_*, por eso también se linkea buffer.c
$(BUILD)/test_netutils: src/lib/netutils_test.c src/lib/netutils.c src/lib/buffer.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_parser: src/lib/parser_test.c src/lib/parser.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_parser_utils: src/lib/parser_utils_test.c src/lib/parser.c src/lib/parser_utils.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_stm: src/lib/stm_test.c src/lib/selector.c src/lib/stm.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_hello: test/hello_test.c src/socks5/hello.c src/lib/buffer.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_auth: test/auth_test.c src/socks5/auth.c src/lib/buffer.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

$(BUILD)/test_request: test/request_test.c src/socks5/request.c src/lib/buffer.c
	@mkdir -p $(BUILD)
	$(TEST_CC) -o $@ $^ $(CHECK_LIBS)

# ── Limpieza ──────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD) socks5d mgmt-client socks5d-debug mgmt-client-debug
