/*
 * auth.c - Servicio de autenticacion separado (PMCD/1.0, Fase 3).
 *
 * Componente independiente del servidor central, con su PROPIO almacen de
 * credenciales y perfiles de usuario. El enunciado (requerimiento 6) pide
 * evitar un sistema de usuarios basado unicamente en la aplicacion principal;
 * por eso este servicio corre como un proceso aparte, en su propio puerto, y
 * el servidor central le consulta por sockets para validar credenciales y
 * obtener un token de sesion.
 *
 * Uso:
 *     ./auth <puerto>
 *
 * Protocolo (reutiliza el framing PMCD):
 *   - Recibe  AUTH_REQ  con payload  usuario=<u>;clave=<c>
 *   - Responde AUTH_RESP con payload  resultado=OK;rol=<rol>;token=<tok>
 *                        o           resultado=FALLO;motivo=<...>
 *
 * El token es opaco (cadena hexadecimal aleatoria). El servicio no guarda
 * sesiones: solo valida credenciales y emite el token; la gestion de la
 * sesion (expiracion, validacion posterior) vive en el servidor central.
 *
 * Compilar/ejecutar en WSL/Linux.
 */

#include "log_utils.h"
#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AUTH_BACKLOG 8

/* ------------------------------------------------------------------ */
/* Almacen de credenciales (propio de este servicio)                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *usuario;
    const char *clave;
    const char *rol;   /* administrador | operador */
} credencial_t;

/* Base de usuarios de ejemplo. En un sistema real estaria en un almacen
   persistente propio del servicio; aqui basta una tabla en memoria, que ya
   cumple el objetivo de estar SEPARADA de la aplicacion principal. */
static const credencial_t g_credenciales[] = {
    { "admin",    "admin123", "administrador" },
    { "operador", "oper123",  "operador"      },
};
static const int g_num_credenciales =
    (int)(sizeof(g_credenciales) / sizeof(g_credenciales[0]));

/* Devuelve el rol si usuario+clave son validos, o NULL si no. */
static const char *validar_credenciales(const char *usuario,
                                        const char *clave) {
    if (usuario == NULL || clave == NULL) {
        return NULL;
    }
    for (int i = 0; i < g_num_credenciales; ++i) {
        if (strcmp(usuario, g_credenciales[i].usuario) == 0 &&
            strcmp(clave, g_credenciales[i].clave) == 0) {
            return g_credenciales[i].rol;
        }
    }
    return NULL;
}

/* Genera un token opaco: 16 caracteres hexadecimales aleatorios. */
static void generar_token(char *out, size_t cap) {
    static const char hex[] = "0123456789abcdef";
    size_t n = (cap > 17) ? 16 : (cap - 1);
    for (size_t i = 0; i < n; ++i) {
        out[i] = hex[rand() & 0x0F];
    }
    out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Atencion de una solicitud                                           */
/* ------------------------------------------------------------------ */

static int enviar(int fd, pmcd_tipo_t tipo, uint32_t seq,
                  const char *payload) {
    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, tipo, PMCD_FLAG_ES_RESPUESTA, seq);
    if (pmcd_pack(buf, sizeof(buf), &h, payload,
                  payload ? strlen(payload) : 0, &total) != PMCD_OK) {
        return -1;
    }
    return su_send_all(fd, buf, total);
}

static void atender(int cli_fd, const char *endpoint) {
    pmcd_header_t h;
    char payload[PMCD_MAX_PAYLOAD];

    int r = su_recv_pmcd_msg(cli_fd, &h, payload, sizeof(payload));
    if (r != 1) {
        log_evento(LOG_INFO, endpoint, "AUTH", "recepcion invalida o cierre");
        su_close(cli_fd);
        return;
    }

    if (h.tipo != PMCD_AUTH_REQ) {
        log_evento(LOG_PETICION, endpoint, pmcd_tipo_nombre(h.tipo),
                   "tipo no soportado por el servicio de auth");
        char resp[PMCD_MAX_PAYLOAD] = "";
        pmcd_build_payload(resp, sizeof(resp), "resultado", "FALLO");
        pmcd_build_payload(resp, sizeof(resp), "motivo", "tipo_invalido");
        enviar(cli_fd, PMCD_AUTH_RESP, h.seq_num, resp);
        su_close(cli_fd);
        return;
    }

    char usuario[64] = "", clave[64] = "";
    pmcd_get_field(payload, "usuario", usuario, sizeof(usuario));
    pmcd_get_field(payload, "clave", clave, sizeof(clave));

    {
        char detalle[128];
        snprintf(detalle, sizeof(detalle), "seq=%u usuario=%s",
                 h.seq_num, usuario);
        log_evento(LOG_PETICION, endpoint, "AUTH_REQ", detalle);
    }

    const char *rol = validar_credenciales(usuario, clave);
    char resp[PMCD_MAX_PAYLOAD] = "";

    if (rol != NULL) {
        char token[32];
        generar_token(token, sizeof(token));
        pmcd_build_payload(resp, sizeof(resp), "resultado", "OK");
        pmcd_build_payload(resp, sizeof(resp), "rol", rol);
        pmcd_build_payload(resp, sizeof(resp), "token", token);
        {
            char detalle[160];
            snprintf(detalle, sizeof(detalle),
                     "seq=%u usuario=%s rol=%s token=%s",
                     h.seq_num, usuario, rol, token);
            log_evento(LOG_RESPUESTA, endpoint, "AUTH_RESP", detalle);
        }
    } else {
        pmcd_build_payload(resp, sizeof(resp), "resultado", "FALLO");
        pmcd_build_payload(resp, sizeof(resp), "motivo", "credenciales_invalidas");
        log_evento(LOG_RESPUESTA, endpoint, "AUTH_RESP",
                   "resultado=FALLO credenciales_invalidas");
    }

    enviar(cli_fd, PMCD_AUTH_RESP, h.seq_num, resp);
    su_close(cli_fd);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *puerto = argv[1];
    char *fin = NULL;
    long p = strtol(puerto, &fin, 10);
    if (fin == puerto || *fin != '\0' || p < 1 || p > 65535) {
        fprintf(stderr, "Puerto invalido: '%s' (use 1..65535)\n", puerto);
        return EXIT_FAILURE;
    }

    srand((unsigned)(time(NULL) ^ getpid()));
    su_ignorar_sigpipe();

    /* El servicio de auth tambien registra en consola (sin archivo). Usamos
       log_utils sin abrir archivo: solo saldra por stdout. */
    printf("Servicio de autenticacion PMCD escuchando en el puerto %s\n",
           puerto);

    int fd = su_crear_socket_servidor_tcp(puerto, AUTH_BACKLOG);
    if (fd < 0) {
        fprintf(stderr, "No se pudo crear el socket del servicio de auth.\n");
        return EXIT_FAILURE;
    }

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cli_fd = accept(fd, (struct sockaddr *)&cli, &clilen);
        if (cli_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept(auth)");
            continue;
        }
        char endpoint[SU_ENDPOINT_STRLEN];
        su_endpoint_str((struct sockaddr *)&cli, clilen,
                        endpoint, sizeof(endpoint));
        atender(cli_fd, endpoint);
    }

    su_close(fd);
    return EXIT_SUCCESS;
}
