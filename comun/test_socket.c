/*
 * test_socket.c - Pruebas de socket_utils.
 *
 *  - Resolucion de destino UDP para localhost (exito) y nombre invalido (error).
 *  - Intercambio de un mensaje PMCD completo por TCP: un hilo actua de servidor
 *    (crea socket de escucha, accept, recibe REG_REQ, responde REG_RESP) y el
 *    hilo principal se conecta por NOMBRE ("localhost"), envia y recibe.
 *
 * Compilar/ejecutar:  make test-socket
 */

#include "socket_utils.h"
#include "pmcd_protocol.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PUERTO_PRUEBA "54999"

static int fallos = 0;
static int pruebas = 0;

static void check(const char *nombre, int cond) {
    pruebas++;
    printf(cond ? "  [OK]   %s\n" : "  [FALLA] %s\n", nombre);
    if (!cond) fallos++;
}

/* Hilo servidor: acepta una conexion, recibe un mensaje y responde. */
static void *hilo_servidor(void *arg) {
    int listen_fd = *(int *)arg;
    int cli = accept(listen_fd, NULL, NULL);
    if (cli < 0) {
        return NULL;
    }

    pmcd_header_t h;
    char payload[PMCD_MAX_PAYLOAD];
    int r = su_recv_pmcd_msg(cli, &h, payload, sizeof(payload));
    if (r == 1 && h.tipo == PMCD_REG_REQ) {
        /* Responder REG_RESP. */
        char resp_payload[PMCD_MAX_PAYLOAD] = "";
        pmcd_build_payload(resp_payload, sizeof(resp_payload),
                           "resultado", "ACEPTADO");
        uint8_t buf[PMCD_MAX_MSG];
        pmcd_header_t rh;
        size_t total = 0;
        pmcd_header_init(&rh, PMCD_REG_RESP, PMCD_FLAG_ES_RESPUESTA, h.seq_num);
        pmcd_pack(buf, sizeof(buf), &rh, resp_payload,
                  strlen(resp_payload), &total);
        su_send_all(cli, buf, total);
    }
    su_close(cli);
    return NULL;
}

int main(void) {
    printf("== Pruebas de socket_utils ==\n");
    su_ignorar_sigpipe();

    /* 1. Resolucion de nombres. */
    printf("\n-- getaddrinfo --\n");
    {
        struct sockaddr_storage addr;
        socklen_t len;
        check("resolver localhost:54999 (exito)",
              su_resolver_destino_udp("localhost", PUERTO_PRUEBA, &addr, &len) == 0);
        check("resolver nombre invalido (error, sin crash)",
              su_resolver_destino_udp("nombre.que.no.existe.invalcxz",
                                      PUERTO_PRUEBA, &addr, &len) == -1);
    }

    /* 2. Intercambio PMCD por TCP resolviendo host por nombre. */
    printf("\n-- Intercambio PMCD por TCP (localhost) --\n");
    int listen_fd = su_crear_socket_servidor_tcp(PUERTO_PRUEBA, 4);
    check("crear socket servidor TCP", listen_fd >= 0);

    if (listen_fd >= 0) {
        pthread_t th;
        pthread_create(&th, NULL, hilo_servidor, &listen_fd);

        int cli = su_conectar_cliente_tcp("localhost", PUERTO_PRUEBA);
        check("conectar cliente por nombre", cli >= 0);

        if (cli >= 0) {
            /* Enviar REG_REQ. */
            char payload[PMCD_MAX_PAYLOAD] = "";
            pmcd_build_payload(payload, sizeof(payload), "nodo_id", "N001");
            pmcd_build_payload(payload, sizeof(payload), "tipo", "sensor");
            uint8_t buf[PMCD_MAX_MSG];
            pmcd_header_t h;
            size_t total = 0;
            pmcd_header_init(&h, PMCD_REG_REQ, PMCD_FLAG_REQUIERE_ACK, 7);
            pmcd_pack(buf, sizeof(buf), &h, payload, strlen(payload), &total);
            check("enviar REG_REQ", su_send_all(cli, buf, total) == 0);

            /* Recibir REG_RESP. */
            pmcd_header_t rh;
            char rpayload[PMCD_MAX_PAYLOAD];
            int r = su_recv_pmcd_msg(cli, &rh, rpayload, sizeof(rpayload));
            check("recibir REG_RESP", r == 1 && rh.tipo == PMCD_REG_RESP);

            char val[64];
            check("REG_RESP correlacionado por seq_num", rh.seq_num == 7);
            check("REG_RESP resultado=ACEPTADO",
                  pmcd_get_field(rpayload, "resultado", val, sizeof(val)) == 1 &&
                  strcmp(val, "ACEPTADO") == 0);

            su_close(cli);
        }

        pthread_join(th, NULL);
        su_close(listen_fd);
    }

    printf("\n== Resultado: %d/%d pruebas OK ==\n", pruebas - fallos, pruebas);
    return fallos == 0 ? 0 : 1;
}
