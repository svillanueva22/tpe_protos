.PHONY: all clean test

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -D_POSIX_C_SOURCE=200112L -pthread
LDFLAGS = -pthread
BUILD   = build

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
    src/server/main.c \
    src/server/args.c

SOCKS5_SRCS = \
    # src/socks5/socks5nio.c  # TODO Fase 1: descomentar cuando hello.h y request.h existan

UTILS_SRCS = \
    src/utils/logger.c \
    src/utils/util.c

ALL_SRCS = $(LIB_SRCS) $(SERVER_SRCS) $(SOCKS5_SRCS) $(UTILS_SRCS)
ALL_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(ALL_SRCS))

# ── Targets principales ───────────────────────────────────────────────

all: socks5d mgmt-client

socks5d: $(ALL_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

mgmt-client:
	@echo "mgmt-client: pendiente de implementación"

# ── Compilación de objetos ────────────────────────────────────────────

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ── Tests (requiere libcheck: apt install check) ──────────────────────

CHECKLIBS = -lcheck -lsubunit -lm -lpthread

test: $(BUILD)/test_buffer $(BUILD)/test_parser $(BUILD)/test_parser_utils \
      $(BUILD)/test_selector $(BUILD)/test_stm
	@for t in $^; do echo "── $$t ──"; $$t; done

# buffer_test y selector_test incluyen el .c directamente → compilar solos
$(BUILD)/test_buffer: src/lib/buffer_test.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(CHECKLIBS)

$(BUILD)/test_selector: src/lib/selector_test.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(CHECKLIBS)

$(BUILD)/test_parser: src/lib/parser_test.c src/lib/parser.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(CHECKLIBS)

$(BUILD)/test_parser_utils: src/lib/parser_utils_test.c src/lib/parser.c src/lib/parser_utils.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(CHECKLIBS)

$(BUILD)/test_stm: src/lib/stm_test.c src/lib/selector.c src/lib/stm.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(CHECKLIBS)

# ── Limpieza ──────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD) socks5d mgmt-client
