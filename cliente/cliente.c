/*
 * cliente.c - Cliente de administracion del Sistema de Monitoreo y Control
 * Distribuido (PMCD/1.0).
 *
 * Uso:
 *     ./cliente <host> <puerto> <usuario> <clave> [comando]
 *
 *   host      nombre/direccion del servidor (resuelto por getaddrinfo).
 *   puerto    puerto del servidor.
 *   usuario   usuario para autenticarse.
 *   clave     clave del usuario.
 *   comando   (opcional) ejecuta un solo comando y termina. Si se omite,
 *             entra en modo interactivo.
 *
 * Comandos:
 *   estado <nodo_id>          estado instantaneo de un nodo
 *   historico <nodo_id> [n]   ultimas n muestras (def. 5)
 *   resumen                   lista de nodos y su estado
 *   salir                     termina el cliente
 *
 * Fase 3: la autenticacion es real (el servidor consulta al servicio de auth).
 * El cliente guarda usuario/clave y el token; si una consulta falla por token
 * expirado o invalido (ERROR 401), se RE-AUTENTICA automaticamente una vez y
 * reintenta la operacion.
 */

#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t g_seq = 1;

/* Estado de sesion del cliente, para poder re-autenticar de forma automatica
   cuando el token expira o el servidor lo invalida. */
static char g_host[128]    = "";
static char g_puerto[16]   = "";
static char g_usuario[64]  = "";
static char g_clave[64]    = "";
static char g_token[64]    = "";

/* Abre una conexion, envia (tipo,payload), lee la respuesta a rpayload y
   devuelve el tipo de la respuesta (o -1 en error). Cierra la conexion. */
static int transaccion(const char *host, const char *puerto,
                       pmcd_tipo_t tipo, const char *payload,
                       char *rpayload, size_t rcap) {
    int fd = su_conectar_cliente_tcp(host, puerto);
    if (fd < 0) {
        return -1;
    }

    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h;
    size_t total = 0;
    pmcd_header_init(&h, tipo, PMCD_FLAG_REQUIERE_ACK, g_seq++);
    if (pmcd_pack(buf, sizeof(buf), &h, payload,
                  payload ? strlen(payload) : 0, &total) != PMCD_OK) {
        su_close(fd);
        return -1;
    }
    if (su_send_all(fd, buf, total) != 0) {
        su_close(fd);
        return -1;
    }

    pmcd_header_t rh;
    int r = su_recv_pmcd_msg(fd, &rh, rpayload, rcap);
    su_close(fd);

    if (r != 1) {
        return -1;
    }
    return (int)rh.tipo;
}

/* Autenticacion real (Fase 3): envia usuario+clave; el servidor consulta al
   servicio de auth. Guarda el token en g_token. Retorna 0/-1.
   Si 'verboso' es 0, no imprime el mensaje de exito (para la re-auth silenciosa). */
static int autenticar_ex(int verboso) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "usuario", g_usuario);
    pmcd_build_payload(payload, sizeof(payload), "clave", g_clave);

    char resp[PMCD_MAX_PAYLOAD];
    int tipo = transaccion(g_host, g_puerto, PMCD_AUTH_REQ, payload,
                           resp, sizeof(resp));
    if (tipo < 0) {
        fprintf(stderr, "No se pudo contactar al servidor para autenticar.\n");
        return -1;
    }
    if (tipo == PMCD_AUTH_RESP) {
        if (pmcd_get_field(resp, "token", g_token, sizeof(g_token)) == 1 &&
            g_token[0] != '\0') {
            char rol[32] = "";
            pmcd_get_field(resp, "rol", rol, sizeof(rol));
            if (verboso) {
                printf("Autenticado como '%s' (rol=%s). token=%s\n",
                       g_usuario, rol[0] ? rol : "?", g_token);
            }
            return 0;
        }
        fprintf(stderr, "AUTH_RESP sin token: %s\n", resp);
        return -1;
    }
    fprintf(stderr, "Autenticacion rechazada: %s\n", resp);
    return -1;
}

static void mostrar_respuesta(int tipo, const char *resp) {
    if (tipo == PMCD_QUERY_RESP) {
        printf("--- Respuesta del servidor ---\n%s\n", resp);
    } else if (tipo == PMCD_ERROR) {
        printf("[ERROR del servidor] %s\n", resp);
    } else if (tipo < 0) {
        printf("[fallo de comunicacion con el servidor]\n");
    } else {
        printf("[respuesta inesperada tipo=%s] %s\n",
               pmcd_tipo_nombre((uint8_t)tipo), resp);
    }
}

/* Devuelve 1 si el payload de un ERROR corresponde a token invalido/expirado
   (codigo 401), para decidir si conviene re-autenticar. */
static int es_error_token(int tipo, const char *resp) {
    if (tipo != PMCD_ERROR) {
        return 0;
    }
    char codigo[8] = "";
    pmcd_get_field(resp, "codigo", codigo, sizeof(codigo));
    return strcmp(codigo, "401") == 0;
}

/*
 * consultar: envia un QUERY_REQ construido por el llamador (sin el token, que
 * agrega esta funcion desde g_token). Si el servidor responde ERROR 401
 * (token invalido/expirado), se RE-AUTENTICA una vez y reintenta.
 *
 *  campos_extra   payload sin token (p.ej. "nodo_id=..;consulta=..")
 */
static void consultar(const char *campos_extra) {
    char payload[PMCD_MAX_PAYLOAD];
    char resp[PMCD_MAX_PAYLOAD];

    snprintf(payload, sizeof(payload), "token=%s;%s", g_token, campos_extra);
    int tipo = transaccion(g_host, g_puerto, PMCD_QUERY_REQ, payload,
                           resp, sizeof(resp));

    if (es_error_token(tipo, resp)) {
        printf("[token invalido/expirado: re-autenticando...]\n");
        if (autenticar_ex(0) == 0) {
            snprintf(payload, sizeof(payload), "token=%s;%s",
                     g_token, campos_extra);
            tipo = transaccion(g_host, g_puerto, PMCD_QUERY_REQ, payload,
                               resp, sizeof(resp));
        }
    }
    mostrar_respuesta(tipo, resp);
}

static void cmd_estado(const char *nodo_id) {
    char extra[128];
    snprintf(extra, sizeof(extra), "nodo_id=%s;consulta=INSTANTANEO", nodo_id);
    consultar(extra);
}

static void cmd_historico(const char *nodo_id, int n) {
    char extra[128];
    snprintf(extra, sizeof(extra), "nodo_id=%s;consulta=HISTORICO;n=%d",
             nodo_id, n);
    consultar(extra);
}

static void cmd_resumen(void) {
    consultar("consulta=RESUMEN");
}

/* Ejecuta un comando ya tokenizado (argv-style). Retorna 1 para continuar,
   0 para salir. */
static int ejecutar(char **tok, int ntok) {
    if (ntok == 0) {
        return 1;
    }
    if (strcmp(tok[0], "salir") == 0 || strcmp(tok[0], "exit") == 0) {
        return 0;
    }
    if (strcmp(tok[0], "estado") == 0 && ntok >= 2) {
        cmd_estado(tok[1]);
    } else if (strcmp(tok[0], "historico") == 0 && ntok >= 2) {
        int n = (ntok >= 3) ? atoi(tok[2]) : 5;
        if (n <= 0) n = 5;
        cmd_historico(tok[1], n);
    } else if (strcmp(tok[0], "resumen") == 0) {
        cmd_resumen();
    } else {
        printf("Comandos: estado <nodo_id> | historico <nodo_id> [n] | "
               "resumen | salir\n");
    }
    return 1;
}

/* Tokeniza 'linea' en palabras separadas por espacios. */
static int tokenizar(char *linea, char **tok, int max) {
    int n = 0;
    char *p = strtok(linea, " \t\r\n");
    while (p != NULL && n < max) {
        tok[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    return n;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <host> <puerto> <usuario> <clave> [comando...]\n"
                "  Comandos: estado <nodo_id> | historico <nodo_id> [n] | "
                "resumen\n", argv[0]);
        return EXIT_FAILURE;
    }

    su_ignorar_sigpipe();

    /* Guardar la sesion en el estado global (para re-autenticacion). */
    snprintf(g_host, sizeof(g_host), "%s", argv[1]);
    snprintf(g_puerto, sizeof(g_puerto), "%s", argv[2]);
    snprintf(g_usuario, sizeof(g_usuario), "%s", argv[3]);
    snprintf(g_clave, sizeof(g_clave), "%s", argv[4]);

    if (autenticar_ex(1) != 0) {
        return EXIT_FAILURE;
    }

    /* Modo comando unico: argv[5..] forman un comando. */
    if (argc > 5) {
        ejecutar(&argv[5], argc - 5);
        return EXIT_SUCCESS;
    }

    /* Modo interactivo. */
    printf("Modo interactivo. Escribe 'salir' para terminar.\n");
    printf("Comandos: estado <nodo_id> | historico <nodo_id> [n] | resumen\n");

    char linea[256];
    for (;;) {
        printf("pmcd> ");
        fflush(stdout);
        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            break; /* EOF */
        }
        char *tok[8];
        int ntok = tokenizar(linea, tok, 8);
        if (!ejecutar(tok, ntok)) {
            break;
        }
    }

    printf("Cliente cerrado.\n");
    return EXIT_SUCCESS;
}
