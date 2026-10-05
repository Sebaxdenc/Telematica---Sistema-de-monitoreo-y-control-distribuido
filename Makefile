# Makefile - Fase 2 PMCD/1.0
# Compilar y ejecutar en WSL/Linux (headers POSIX + pthread).

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -pthread -Icomun -D_POSIX_C_SOURCE=200112L
LDFLAGS = -pthread

BIN     = bin
COMUN   = comun

# Objetos compartidos por todos los componentes
COMUN_SRC = $(COMUN)/pmcd_protocol.c $(COMUN)/socket_utils.c $(COMUN)/log_utils.c

# Fuentes propias del servidor (estado + logica de protocolo)
SERVIDOR_SRC = servidor/servidor.c servidor/estado.c servidor/manejador.c servidor/udp_worker.c \
               servidor/sesiones.c servidor/auth_cliente.c servidor/monitor.c

# Ejecutables
SERVIDOR = $(BIN)/servidor
NODO     = $(BIN)/nodo
CLIENTE  = $(BIN)/cliente
AUTH     = $(BIN)/auth

.PHONY: all servidor nodo cliente auth test test-socket clean

all: servidor nodo cliente auth

servidor: $(SERVIDOR)
nodo:     $(NODO)
cliente:  $(CLIENTE)
auth:     $(AUTH)

$(SERVIDOR): $(SERVIDOR_SRC) $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) -Iservidor $^ -o $@ $(LDFLAGS)

$(NODO): nodo/nodo.c $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENTE): cliente/cliente.c $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(AUTH): auth/auth.c $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Prueba de round-trip del protocolo (se habilita en Task 2)
test: $(BIN)/test_protocolo
	./$(BIN)/test_protocolo

$(BIN)/test_protocolo: comun/test_protocolo.c $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Prueba de las utilidades de socket
test-socket: $(BIN)/test_socket
	./$(BIN)/test_socket

$(BIN)/test_socket: comun/test_socket.c $(COMUN_SRC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
	rm -f *.log
