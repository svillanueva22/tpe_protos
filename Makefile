.PHONY: all clean

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -fsanitize=address -g

UTILS_DIR    = src/utils
SERVER_DIR   = src/server
SOCKS5_DIR   = src/socks5
MGMT_DIR     = src/mgmt
CLIENT_DIR   = src-client

UTILS_SRC = $(UTILS_DIR)/logger.c $(UTILS_DIR)/util.c \
            $(UTILS_DIR)/tcpClientUtil.c $(UTILS_DIR)/tcpServerUtil.c

all:
	@echo "No hay targets aún. Implementar servidor y cliente."

clean:
	- rm -f *.o socks5d mgmt-client
