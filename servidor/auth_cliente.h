#ifndef AUTH_CLIENTE_H
#define AUTH_CLIENTE_H

/*
 * auth_cliente.h - Cliente del servicio de autenticacion (lado servidor).
 *
 * El servidor central usa esta funcion para consultar al servicio de auth
 * (un proceso separado) y validar las credenciales de un cliente. Abre una
 * conexion TCP al servicio, envia AUTH_REQ y espera AUTH_RESP.
 */

#include <stddef.h>

/* Resultado de una consulta al servicio de auth. */
typedef enum {
    AUTHC_OK          =  0, /* credenciales validas: hay rol y token         */
    AUTHC_RECHAZADO   = -1, /* credenciales invalidas                        */
    AUTHC_SIN_SERVICIO= -2  /* no se pudo contactar/usar el servicio de auth */
} authc_res_t;

/*
 * auth_cliente_validar: consulta al servicio de auth en auth_host:auth_puerto.
 *
 *  auth_host, auth_puerto  ubicacion del servicio (resueltos por getaddrinfo)
 *  usuario, clave          credenciales a validar
 *  out_rol                 [salida] rol devuelto por el servicio (si OK)
 *  out_token               [salida] token emitido (si OK)
 *
 * Retorna un authc_res_t. Ante fallo de comunicacion devuelve
 * AUTHC_SIN_SERVICIO sin terminar el proceso.
 */
int auth_cliente_validar(const char *auth_host, const char *auth_puerto,
                         const char *usuario, const char *clave,
                         char *out_rol, size_t rol_cap,
                         char *out_token, size_t token_cap);

#endif /* AUTH_CLIENTE_H */
