/*
 * auth_cliente.c - Implementacion del cliente del servicio de autenticacion.
 *
 * El servidor central actua aqui como CLIENTE del servicio de auth: abre una
 * conexion TCP, envia un AUTH_REQ con las credenciales y lee el AUTH_RESP.
 * Toda la resolucion pasa por getaddrinfo (dentro de socket_utils), sin IPs
 * codificadas. Si el servicio no responde, se devuelve AUTHC_SIN_SERVICIO y
 * el servidor sigue funcionando.
 */

#include "auth_cliente.h"

#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <stdio.h>
#include <string.h>

/* Numero de secuencia local para las consultas al servicio de auth. */
static uint32_t g_seq = 1;

int auth_cliente_validar(const char *auth_host, const char *auth_puerto,
                         const char *usuario, const char *clave,
                         char *out_rol, size_t rol_cap,
                         char *out_token, size_t token_cap) {
    if (auth_host == NULL || auth_puerto == NULL ||
        usuario == NULL || clave == NULL) {
        return AUTHC_SIN_SERVICIO;
    }

    int fd = su_conectar_cliente_tcp(auth_host, auth_puerto);
    if (fd < 0) {
        /* No se pudo contactar al servicio (caido o inalcanzable). */
        return AUTHC_SIN_SERVICIO;
    }

    /* Construir AUTH_REQ: usuario=<u>;clave=<c> */
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "usuario", usuario);
    pmcd_build_payload(payload, sizeof(payload), "clave", clave);

    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, PMCD_AUTH_REQ, PMCD_FLAG_REQUIERE_ACK, g_seq++);
    if (pmcd_pack(buf, sizeof(buf), &h, payload, strlen(payload),
                  &total) != PMCD_OK) {
        su_close(fd);
        return AUTHC_SIN_SERVICIO;
    }
    if (su_send_all(fd, buf, total) != 0) {
        su_close(fd);
        return AUTHC_SIN_SERVICIO;
    }

    /* Leer AUTH_RESP. */
    pmcd_header_t rh;
    char rpayload[PMCD_MAX_PAYLOAD];
    int r = su_recv_pmcd_msg(fd, &rh, rpayload, sizeof(rpayload));
    su_close(fd);

    if (r != 1 || rh.tipo != PMCD_AUTH_RESP) {
        return AUTHC_SIN_SERVICIO;
    }

    char resultado[16] = "";
    pmcd_get_field(rpayload, "resultado", resultado, sizeof(resultado));

    if (strcmp(resultado, "OK") == 0) {
        if (out_rol != NULL) {
            pmcd_get_field(rpayload, "rol", out_rol, rol_cap);
        }
        if (out_token != NULL) {
            pmcd_get_field(rpayload, "token", out_token, token_cap);
        }
        return AUTHC_OK;
    }

    return AUTHC_RECHAZADO;
}
