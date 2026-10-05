#ifndef SESIONES_H
#define SESIONES_H

/*
 * sesiones.h - Tabla de sesiones autenticadas (lado servidor central).
 *
 * Cuando un cliente se autentica, el servidor consulta al servicio de auth y,
 * si es valido, guarda aqui una sesion: token -> {usuario, rol, expiracion}.
 * Cada QUERY_REQ posterior trae el token; el servidor lo valida contra esta
 * tabla sin volver a pedir la contrasena ni consultar al servicio de auth.
 *
 * Protegida por un mutex porque varios hilos TCP la leen/escriben.
 */

#include <stddef.h>

#define SES_MAX          64
#define SES_TOKEN_LEN    40
#define SES_USUARIO_LEN  64
#define SES_ROL_LEN      32

/* Vigencia de una sesion en segundos (expiracion del token).
   Se puede sobrescribir en compilacion con -DSES_TTL_SEG=<n> (util en pruebas). */
#ifndef SES_TTL_SEG
#define SES_TTL_SEG      300   /* 5 minutos */
#endif

/* Inicializa la tabla y el mutex. */
void sesiones_init(void);

/*
 * sesiones_crear: registra una sesion con su token, usuario y rol. La
 * expiracion se fija en ahora + SES_TTL_SEG. Si el token ya existe, lo
 * actualiza. Retorna 0 en exito, -1 si la tabla esta llena o args invalidos.
 */
int sesiones_crear(const char *token, const char *usuario, const char *rol);

/*
 * sesiones_validar: comprueba que el token exista y no haya expirado. Si es
 * valido y out_rol != NULL, copia el rol en out_rol. Retorna:
 *    1  token valido
 *    0  token inexistente
 *   -1  token expirado (se elimina de la tabla)
 */
int sesiones_validar(const char *token, char *out_rol, size_t rol_cap);

/* Elimina las sesiones expiradas (llamable periodicamente). Retorna cuantas
   elimino. */
int sesiones_limpiar_expiradas(void);

#endif /* SESIONES_H */
