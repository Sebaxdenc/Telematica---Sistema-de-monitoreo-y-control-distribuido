/*
 * manejador.c - Atencion de una conexion TCP segun el protocolo PMCD/1.0.
 *
 * Implementa las reglas basicas del protocolo (Fase 1):
 *   - REG_REQ   -> registrar nodo, responder REG_RESP.
 *   - EVENT     -> exige nodo registrado; registrar actividad, responder
 *                  EVENT_ACK (confirmacion de aplicacion).
 *   - AUTH_REQ  -> autenticacion stub (Fase 2): se acepta y se emite un token
 *                  dummy en AUTH_RESP. La validacion real es de Fase 3.
 *   - QUERY_REQ -> exige token no vacio (stub); responder QUERY_RESP con el
 *                  estado instantaneo o el historico.
 *   - DISCONNECT-> cierre voluntario de la sesion.
 *   - Mensajes de nodo no registrado / formato invalido / token ausente ->
 *     ERROR con el codigo correspondiente.
 *
 * En Fase 2 cada conexion transporta una operacion (peticion -> respuesta) y
 * luego se cierra; el bucle interno permite, ademas, varias operaciones en la
 * misma conexion hasta que el otro extremo la cierra o envia DISCONNECT.
 */

#include "manejador.h"
#include "estado.h"

#include "log_utils.h"
#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Codigos de error de aplicacion (payload de ERROR). */
#define ERR_FORMATO_INVALIDO   "400"
#define ERR_NO_AUTORIZADO      "401"
#define ERR_NODO_NO_REGISTRADO "409"
#define ERR_TIPO_NO_SOPORTADO  "422"

/* Envia un mensaje PMCD (tipo + payload) por el socket. Retorna 0/-1. */
static int enviar(int fd, pmcd_tipo_t tipo, uint8_t flags,
                  uint32_t seq_num, const char *payload,
                  const char *endpoint) {
    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    size_t plen = (payload != NULL) ? strlen(payload) : 0;

    pmcd_header_init(&h, tipo, flags, seq_num);
    int r = pmcd_pack(buf, sizeof(buf), &h, payload, plen, &total);
    if (r != PMCD_OK) {
        return -1;
    }

    char detalle[PMCD_MAX_PAYLOAD + 32];
    snprintf(detalle, sizeof(detalle), "seq=%u %s",
             seq_num, (payload != NULL) ? payload : "");
    log_evento(LOG_RESPUESTA, endpoint, pmcd_tipo_nombre((uint8_t)tipo), detalle);

    return su_send_all(fd, buf, total);
}

/* Envia un ERROR con codigo y descripcion. */
static int enviar_error(int fd, uint32_t seq_num, const char *codigo,
                        const char *descripcion, const char *endpoint) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "codigo", codigo);
    pmcd_build_payload(payload, sizeof(payload), "descripcion", descripcion);
    return enviar(fd, PMCD_ERROR, PMCD_FLAG_ES_RESPUESTA, seq_num,
                  payload, endpoint);
}

/* --- Handlers por tipo de mensaje --- */

static int on_reg_req(int fd, const pmcd_header_t *h, const char *payload,
                      const char *endpoint) {
    char id[64] = "", tipo[64] = "";
    if (pmcd_get_field(payload, "nodo_id", id, sizeof(id)) != 1 ||
        id[0] == '\0') {
        return enviar_error(fd, h->seq_num, ERR_FORMATO_INVALIDO,
                            "falta nodo_id", endpoint);
    }
    pmcd_get_field(payload, "tipo", tipo, sizeof(tipo));

    int r = estado_registrar_nodo(id, tipo, endpoint);
    if (r == -1) {
        return enviar_error(fd, h->seq_num, ERR_TIPO_NO_SOPORTADO,
                            "tabla de nodos llena", endpoint);
    }

    char resp[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(resp, sizeof(resp), "resultado", "ACEPTADO");
    pmcd_build_payload(resp, sizeof(resp), "nodo_id", id);
    return enviar(fd, PMCD_REG_RESP, PMCD_FLAG_ES_RESPUESTA, h->seq_num,
                  resp, endpoint);
}

static int on_event(int fd, const pmcd_header_t *h, const char *payload,
                    const char *endpoint) {
    char id[64] = "";
    if (pmcd_get_field(payload, "nodo_id", id, sizeof(id)) != 1 ||
        id[0] == '\0') {
        return enviar_error(fd, h->seq_num, ERR_FORMATO_INVALIDO,
                            "falta nodo_id", endpoint);
    }
    if (!estado_nodo_registrado(id)) {
        return enviar_error(fd, h->seq_num, ERR_NODO_NO_REGISTRADO,
                            "nodo no registrado", endpoint);
    }

    estado_marcar_actividad(id);
    /* El evento tambien queda en el log (ya se logueo como PETICION). */

    char resp[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(resp, sizeof(resp), "resultado", "PROCESADO");
    pmcd_build_payload(resp, sizeof(resp), "nodo_id", id);
    return enviar(fd, PMCD_EVENT_ACK, PMCD_FLAG_ES_RESPUESTA, h->seq_num,
                  resp, endpoint);
}

static int on_auth_req(int fd, const pmcd_header_t *h, const char *payload,
                       const char *endpoint) {
    /* Stub de Fase 2: aceptamos cualquier usuario no vacio y emitimos un
       token dummy. La validacion real de credenciales es de Fase 3. */
    char usuario[64] = "";
    pmcd_get_field(payload, "usuario", usuario, sizeof(usuario));

    if (usuario[0] == '\0') {
        return enviar_error(fd, h->seq_num, ERR_FORMATO_INVALIDO,
                            "falta usuario", endpoint);
    }

    char resp[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(resp, sizeof(resp), "resultado", "OK");
    /* Token dummy fijo para Fase 2. */
    pmcd_build_payload(resp, sizeof(resp), "token", "TOKEN-FASE2");
    return enviar(fd, PMCD_AUTH_RESP, PMCD_FLAG_ES_RESPUESTA, h->seq_num,
                  resp, endpoint);
}

static int on_query_req(int fd, const pmcd_header_t *h, const char *payload,
                        const char *endpoint) {
    char token[64] = "", id[64] = "", consulta[32] = "", n_str[16] = "";

    /* Stub de auth: exigimos token no vacio (no validamos su contenido). */
    if (pmcd_get_field(payload, "token", token, sizeof(token)) != 1 ||
        token[0] == '\0') {
        return enviar_error(fd, h->seq_num, ERR_NO_AUTORIZADO,
                            "token ausente o invalido", endpoint);
    }

    pmcd_get_field(payload, "nodo_id", id, sizeof(id));
    pmcd_get_field(payload, "consulta", consulta, sizeof(consulta));

    char resp[PMCD_MAX_PAYLOAD];

    /* Consulta general (sin nodo_id) -> resumen de todos los nodos. */
    if (id[0] == '\0') {
        char resumen[PMCD_MAX_PAYLOAD - 64] = "";
        estado_resumen(resumen, sizeof(resumen));
        snprintf(resp, sizeof(resp), "tipo=RESUMEN;nodos=%s", resumen);
        return enviar(fd, PMCD_QUERY_RESP, PMCD_FLAG_ES_RESPUESTA,
                      h->seq_num, resp, endpoint);
    }

    if (strcmp(consulta, "HISTORICO") == 0) {
        int n = 5;
        if (pmcd_get_field(payload, "n", n_str, sizeof(n_str)) == 1) {
            int v = atoi(n_str);
            if (v > 0) {
                n = v;
            }
        }
        char hist[PMCD_MAX_PAYLOAD - 128] = "";
        int escritas = estado_consulta_historico(id, n, hist, sizeof(hist));
        if (escritas < 0) {
            return enviar_error(fd, h->seq_num, ERR_NODO_NO_REGISTRADO,
                                "nodo no registrado", endpoint);
        }
        snprintf(resp, sizeof(resp),
                 "tipo=HISTORICO;nodo_id=%s;muestras=%d\n%s",
                 id, escritas, hist);
        return enviar(fd, PMCD_QUERY_RESP, PMCD_FLAG_ES_RESPUESTA,
                      h->seq_num, resp, endpoint);
    }

    /* Por defecto: estado instantaneo. */
    char inst[PMCD_MAX_PAYLOAD - 64] = "";
    if (estado_consulta_instantanea(id, inst, sizeof(inst)) != 0) {
        return enviar_error(fd, h->seq_num, ERR_NODO_NO_REGISTRADO,
                            "nodo no registrado", endpoint);
    }
    snprintf(resp, sizeof(resp), "tipo=INSTANTANEO;%s", inst);
    return enviar(fd, PMCD_QUERY_RESP, PMCD_FLAG_ES_RESPUESTA,
                  h->seq_num, resp, endpoint);
}

void manejar_conexion_tcp(int cli_fd, const char *endpoint) {
    pmcd_header_t h;
    char payload[PMCD_MAX_PAYLOAD];

    for (;;) {
        int r = su_recv_pmcd_msg(cli_fd, &h, payload, sizeof(payload));
        if (r == 0) {
            break; /* el otro extremo cerro */
        }
        if (r == -1) {
            log_evento(LOG_INFO, endpoint, "TCP", "error de recepcion");
            break;
        }
        if (r == -2) {
            /* Mensaje corrupto (MAGIC/VERSION/PAYLOAD_LEN). Por TCP si hay
               conexion, respondemos ERROR (Fase 1). */
            log_evento(LOG_PETICION, endpoint, "INVALIDO",
                       "MAGIC/VERSION/longitud invalidos");
            enviar_error(cli_fd, 0, ERR_FORMATO_INVALIDO,
                         "mensaje PMCD invalido", endpoint);
            break;
        }

        /* Log de la peticion entrante. */
        {
            char detalle[PMCD_MAX_PAYLOAD + 32];
            snprintf(detalle, sizeof(detalle), "seq=%u %s", h.seq_num, payload);
            log_evento(LOG_PETICION, endpoint,
                       pmcd_tipo_nombre(h.tipo), detalle);
        }

        int envio = 0;
        switch (h.tipo) {
            case PMCD_REG_REQ:
                envio = on_reg_req(cli_fd, &h, payload, endpoint);
                break;
            case PMCD_EVENT:
                envio = on_event(cli_fd, &h, payload, endpoint);
                break;
            case PMCD_AUTH_REQ:
                envio = on_auth_req(cli_fd, &h, payload, endpoint);
                break;
            case PMCD_QUERY_REQ:
                envio = on_query_req(cli_fd, &h, payload, endpoint);
                break;
            case PMCD_DISCONNECT:
                log_evento(LOG_INFO, endpoint, "DISCONNECT",
                           "cierre voluntario");
                envio = 0;
                su_close(cli_fd);
                return;
            default:
                envio = enviar_error(cli_fd, h.seq_num, ERR_TIPO_NO_SOPORTADO,
                                     "tipo no soportado por TCP", endpoint);
                break;
        }

        if (envio < 0) {
            log_evento(LOG_INFO, endpoint, "TCP", "fallo al enviar respuesta");
            break;
        }
    }

    su_close(cli_fd);
}
