/*
 * nodo.c - Nodo/sensor del Sistema de Monitoreo y Control Distribuido.
 *
 * Uso:
 *     ./nodo <host> <puerto> <nodo_id> [tipo] [intervalo_seg]
 *
 *   host           nombre o direccion del servidor (resuelto por getaddrinfo).
 *   puerto         puerto del servidor (mismo para TCP y UDP en Fase 2).
 *   nodo_id        identificador del nodo (p.ej. N001).
 *   tipo           tipo de nodo (opcional, def. "sensor_generico").
 *   intervalo_seg  segundos entre reportes (opcional, def. 3).
 *
 * Flujo (Fase 2):
 *   1. REG_REQ por TCP y espera REG_RESP.
 *   2. Bucle periodico: envia TELEMETRY por UDP. Cada varias muestras simula
 *      un EVENT critico por TCP (espera EVENT_ACK) y ocasionalmente un
 *      HEARTBEAT por UDP cuando "no hay telemetria nueva".
 *
 * No hay IPs codificadas: todo destino se resuelve por nombre/puerto.
 */

#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Envia REG_REQ por una conexion TCP nueva y espera REG_RESP.
   Retorna 0 si el registro fue aceptado, -1 en error. */
static int registrar(const char *host, const char *puerto,
                     const char *nodo_id, const char *tipo, uint32_t *seq) {
    int fd = su_conectar_cliente_tcp(host, puerto);
    if (fd < 0) {
        fprintf(stderr, "[nodo %s] no se pudo conectar para registro\n", nodo_id);
        return -1;
    }

    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "nodo_id", nodo_id);
    pmcd_build_payload(payload, sizeof(payload), "tipo", tipo);
    pmcd_build_payload(payload, sizeof(payload), "capacidades",
                       "telemetria,eventos");

    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, PMCD_REG_REQ, PMCD_FLAG_REQUIERE_ACK, (*seq)++);
    pmcd_pack(buf, sizeof(buf), &h, payload, strlen(payload), &total);

    if (su_send_all(fd, buf, total) != 0) {
        fprintf(stderr, "[nodo %s] fallo al enviar REG_REQ\n", nodo_id);
        su_close(fd);
        return -1;
    }

    pmcd_header_t rh;
    char rpayload[PMCD_MAX_PAYLOAD];
    int r = su_recv_pmcd_msg(fd, &rh, rpayload, sizeof(rpayload));
    su_close(fd);

    if (r != 1) {
        fprintf(stderr, "[nodo %s] sin respuesta valida al registro (r=%d)\n",
                nodo_id, r);
        return -1;
    }
    if (rh.tipo == PMCD_REG_RESP) {
        char res[32] = "";
        pmcd_get_field(rpayload, "resultado", res, sizeof(res));
        printf("[nodo %s] registro: %s (%s)\n", nodo_id, res, rpayload);
        return 0;
    }
    if (rh.tipo == PMCD_ERROR) {
        printf("[nodo %s] registro rechazado: %s\n", nodo_id, rpayload);
        return -1;
    }
    printf("[nodo %s] respuesta inesperada al registro: %s\n",
           nodo_id, pmcd_tipo_nombre(rh.tipo));
    return -1;
}

/* Envia un datagrama UDP (TELEMETRY o HEARTBEAT) al servidor. */
static int enviar_udp(int udp_fd, const struct sockaddr *dst, socklen_t dstlen,
                      pmcd_tipo_t tipo, uint32_t seq, const char *payload) {
    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;

    pmcd_header_init(&h, tipo, 0, seq);
    if (pmcd_pack(buf, sizeof(buf), &h, payload,
                  payload ? strlen(payload) : 0, &total) != PMCD_OK) {
        return -1;
    }

    ssize_t r = sendto(udp_fd, buf, total, 0, dst, dstlen);
    if (r < 0) {
        perror("sendto");
        return -1;
    }
    return 0;
}

/* Envia un EVENT critico por TCP y espera EVENT_ACK. Retorna 0/-1. */
static int enviar_evento(const char *host, const char *puerto,
                         const char *nodo_id, uint32_t *seq,
                         const char *descripcion) {
    int fd = su_conectar_cliente_tcp(host, puerto);
    if (fd < 0) {
        return -1;
    }

    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "nodo_id", nodo_id);
    pmcd_build_payload(payload, sizeof(payload), "evento", "UMBRAL_SUPERADO");
    pmcd_build_payload(payload, sizeof(payload), "detalle", descripcion);
    pmcd_build_payload(payload, sizeof(payload), "severidad", "ALTA");

    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, PMCD_EVENT, PMCD_FLAG_REQUIERE_ACK, (*seq)++);
    pmcd_pack(buf, sizeof(buf), &h, payload, strlen(payload), &total);

    if (su_send_all(fd, buf, total) != 0) {
        su_close(fd);
        return -1;
    }

    pmcd_header_t rh;
    char rpayload[PMCD_MAX_PAYLOAD];
    int r = su_recv_pmcd_msg(fd, &rh, rpayload, sizeof(rpayload));
    su_close(fd);

    if (r == 1 && rh.tipo == PMCD_EVENT_ACK) {
        printf("[nodo %s] EVENT confirmado (EVENT_ACK): %s\n",
               nodo_id, rpayload);
        return 0;
    }
    printf("[nodo %s] EVENT sin ACK (r=%d)\n", nodo_id, r);
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr,
                "Uso: %s <host> <puerto> <nodo_id> [tipo] [intervalo_seg]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];
    const char *puerto = argv[2];
    const char *nodo_id = argv[3];
    const char *tipo = (argc >= 5) ? argv[4] : "sensor_generico";
    int intervalo = (argc >= 6) ? atoi(argv[5]) : 3;
    if (intervalo <= 0) {
        intervalo = 3;
    }

    su_ignorar_sigpipe();
    uint32_t seq = 1;

    /* 1. Registro (con reintentos: no terminar si falla la resolucion). */
    int intentos = 0;
    while (registrar(host, puerto, nodo_id, tipo, &seq) != 0) {
        intentos++;
        if (intentos >= 5) {
            fprintf(stderr, "[nodo %s] no se pudo registrar tras %d intentos\n",
                    nodo_id, intentos);
            return EXIT_FAILURE;
        }
        fprintf(stderr, "[nodo %s] reintentando registro en 2s...\n", nodo_id);
        sleep(2);
    }

    /* 2. Socket UDP + destino resuelto por nombre. */
    int udp_fd = su_crear_socket_udp_cliente();
    if (udp_fd < 0) {
        return EXIT_FAILURE;
    }
    struct sockaddr_storage dst;
    socklen_t dstlen = 0;
    if (su_resolver_destino_udp(host, puerto, &dst, &dstlen) != 0) {
        fprintf(stderr, "[nodo %s] no se pudo resolver destino UDP\n", nodo_id);
        su_close(udp_fd);
        return EXIT_FAILURE;
    }

    printf("[nodo %s] activo. Enviando telemetria cada %ds a %s:%s (Ctrl+C para salir)\n",
           nodo_id, intervalo, host, puerto);

    /* 3. Bucle de reporte. */
    int ciclo = 0;
    double temp = 35.0;
    int bateria = 100;

    for (;;) {
        ciclo++;
        temp += 1.7;                 /* simula variacion */
        if (temp > 90.0) temp = 35.0;
        if (bateria > 0) bateria--;

        char payload[PMCD_MAX_PAYLOAD] = "";
        char valbuf[32];

        pmcd_build_payload(payload, sizeof(payload), "nodo_id", nodo_id);
        snprintf(valbuf, sizeof(valbuf), "%.1f", 40.0 + (ciclo % 30));
        pmcd_build_payload(payload, sizeof(payload), "cpu", valbuf);
        snprintf(valbuf, sizeof(valbuf), "%.1f", temp);
        pmcd_build_payload(payload, sizeof(payload), "temp", valbuf);
        snprintf(valbuf, sizeof(valbuf), "%d", bateria);
        pmcd_build_payload(payload, sizeof(payload), "bateria", valbuf);
        pmcd_build_payload(payload, sizeof(payload), "estado", "ACTIVO");

        if (enviar_udp(udp_fd, (struct sockaddr *)&dst, dstlen,
                       PMCD_TELEMETRY, seq++, payload) == 0) {
            printf("[nodo %s] TELEMETRY #%d enviada (temp=%.1f bateria=%d)\n",
                   nodo_id, ciclo, temp, bateria);
        }

        /* Cada 3 ciclos, si la temperatura es alta, dispara un EVENT. */
        if (ciclo % 3 == 0 && temp > 80.0) {
            char desc[64];
            snprintf(desc, sizeof(desc), "temp=%.1f supera umbral 80.0", temp);
            enviar_evento(host, puerto, nodo_id, &seq, desc);
        }

        /* Cada 5 ciclos simula "sin telemetria nueva": manda HEARTBEAT. */
        if (ciclo % 5 == 0) {
            char hb[PMCD_MAX_PAYLOAD] = "";
            pmcd_build_payload(hb, sizeof(hb), "nodo_id", nodo_id);
            pmcd_build_payload(hb, sizeof(hb), "estado", "VIVO");
            if (enviar_udp(udp_fd, (struct sockaddr *)&dst, dstlen,
                           PMCD_HEARTBEAT, seq++, hb) == 0) {
                printf("[nodo %s] HEARTBEAT enviado\n", nodo_id);
            }
        }

        sleep((unsigned)intervalo);
    }

    su_close(udp_fd);
    return EXIT_SUCCESS;
}
