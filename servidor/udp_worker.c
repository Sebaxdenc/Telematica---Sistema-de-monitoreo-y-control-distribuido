/*
 * udp_worker.c - Hilo de recepcion UDP del servidor.
 *
 * Los datagramas UDP transportan TELEMETRY y HEARTBEAT (Fase 1). Como UDP no
 * tiene conexion, el servidor no responde en condiciones normales; solo envia
 * un ERROR por el mismo socket cuando el datagrama tiene formato invalido.
 *
 * El acceso a la tabla de estado se hace a traves del modulo 'estado', que ya
 * serializa con su propio mutex; asi este hilo y los hilos TCP comparten la
 * misma estructura sin condiciones de carrera.
 */

#include "udp_worker.h"
#include "estado.h"

#include "log_utils.h"
#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

/* Responde un ERROR por el socket UDP al remitente 'from'. */
static void responder_error_udp(int udp_fd, const struct sockaddr *from,
                                socklen_t fromlen, uint32_t seq,
                                const char *codigo, const char *desc) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "codigo", codigo);
    pmcd_build_payload(payload, sizeof(payload), "descripcion", desc);

    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, PMCD_ERROR, PMCD_FLAG_ES_RESPUESTA, seq);
    if (pmcd_pack(buf, sizeof(buf), &h, payload, strlen(payload),
                  &total) == PMCD_OK) {
        sendto(udp_fd, buf, total, 0, from, fromlen);
    }
}

void *udp_worker(void *arg) {
    udp_worker_args_t *a = (udp_worker_args_t *)arg;
    int udp_fd = a->udp_fd;

    /* Timeout de recepcion para poder revisar la bandera de parada y no
       quedar bloqueados indefinidamente en recvfrom(). */
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(udp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[PMCD_MAX_MSG];

    while (!*(a->parar)) {
        struct sockaddr_storage from;
        socklen_t fromlen = sizeof(from);

        ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue; /* timeout o interrupcion: reintentar */
            }
            perror("recvfrom");
            continue;
        }

        char endpoint[SU_ENDPOINT_STRLEN];
        su_endpoint_str((struct sockaddr *)&from, fromlen,
                        endpoint, sizeof(endpoint));

        /* Desempaquetar. */
        pmcd_header_t h;
        char payload[PMCD_MAX_PAYLOAD];
        int r = pmcd_unpack(buf, (size_t)n, &h, payload, sizeof(payload));
        if (r != PMCD_OK) {
            /* Datagrama corrupto: en UDP el enunciado permite descartar, pero
               si hay error de FORMATO respondemos ERROR por el mismo socket. */
            log_evento(LOG_PETICION, endpoint, "UDP-INVALIDO",
                       "datagrama PMCD invalido");
            responder_error_udp(udp_fd, (struct sockaddr *)&from, fromlen,
                                h.seq_num, "400", "datagrama PMCD invalido");
            continue;
        }

        char id[64] = "";
        pmcd_get_field(payload, "nodo_id", id, sizeof(id));

        if (h.tipo == PMCD_TELEMETRY) {
            char detalle[PMCD_MAX_PAYLOAD + 32];
            snprintf(detalle, sizeof(detalle), "seq=%u %s", h.seq_num, payload);
            log_evento(LOG_PETICION, endpoint, "TELEMETRY", detalle);

            if (id[0] == '\0' || !estado_nodo_registrado(id)) {
                /* Nodo no registrado: en UDP se descarta (sin "siguiente"
                   conexion que justifique respuesta), pero lo dejamos en log. */
                log_evento(LOG_INFO, endpoint, "TELEMETRY",
                           "descartada: nodo no registrado");
                continue;
            }
            estado_agregar_muestra(id, payload);
        } else if (h.tipo == PMCD_HEARTBEAT) {
            char detalle[PMCD_MAX_PAYLOAD + 32];
            snprintf(detalle, sizeof(detalle), "seq=%u %s", h.seq_num, payload);
            log_evento(LOG_PETICION, endpoint, "HEARTBEAT", detalle);

            if (id[0] != '\0' && estado_nodo_registrado(id)) {
                estado_marcar_actividad(id);
            }
        } else {
            /* Tipo no esperado por UDP. */
            log_evento(LOG_PETICION, endpoint, "UDP-INESPERADO",
                       pmcd_tipo_nombre(h.tipo));
            responder_error_udp(udp_fd, (struct sockaddr *)&from, fromlen,
                                h.seq_num, "422",
                                "tipo no soportado por UDP");
        }
    }

    return NULL;
}
