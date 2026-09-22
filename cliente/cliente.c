/*
 * cliente.c - Cliente de administracion del Sistema de Monitoreo y Control
 * Distribuido (PMCD/1.0).
 *
 * Uso:
 *     ./cliente <host> <puerto> <usuario> [comando]
 *
 *   host      nombre/direccion del servidor (resuelto por getaddrinfo).
 *   puerto    puerto del servidor.
 *   usuario   usuario para autenticarse (stub de Fase 2).
 *   comando   (opcional) ejecuta un solo comando y termina. Si se omite,
 *             entra en modo interactivo.
 *
 * Comandos:
 *   estado <nodo_id>          estado instantaneo de un nodo
 *   historico <nodo_id> [n]   ultimas n muestras (def. 5)
 *   resumen                   lista de nodos y su estado
 *   salir                     termina el cliente
 *
 * El cliente se autentica una vez (AUTH_REQ) y guarda el token que el
 * servidor devuelve; ese token se incluye en cada QUERY_REQ. En Fase 2 la
 * validacion es un stub (token dummy); la autenticacion real es de Fase 3.
 */

#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t g_seq = 1;

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

/* Autenticacion (stub). Guarda el token en 'token'. Retorna 0/-1. */
static int autenticar(const char *host, const char *puerto,
                      const char *usuario, char *token, size_t tcap) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "usuario", usuario);
    pmcd_build_payload(payload, sizeof(payload), "clave", "demo");

    char resp[PMCD_MAX_PAYLOAD];
    int tipo = transaccion(host, puerto, PMCD_AUTH_REQ, payload,
                           resp, sizeof(resp));
    if (tipo < 0) {
        fprintf(stderr, "No se pudo contactar al servidor para autenticar.\n");
        return -1;
    }
    if (tipo == PMCD_AUTH_RESP) {
        if (pmcd_get_field(resp, "token", token, tcap) == 1 && token[0] != '\0') {
            printf("Autenticado como '%s'. token=%s\n", usuario, token);
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

static void cmd_estado(const char *host, const char *puerto,
                       const char *token, const char *nodo_id) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "token", token);
    pmcd_build_payload(payload, sizeof(payload), "nodo_id", nodo_id);
    pmcd_build_payload(payload, sizeof(payload), "consulta", "INSTANTANEO");

    char resp[PMCD_MAX_PAYLOAD];
    int tipo = transaccion(host, puerto, PMCD_QUERY_REQ, payload,
                           resp, sizeof(resp));
    mostrar_respuesta(tipo, resp);
}

static void cmd_historico(const char *host, const char *puerto,
                          const char *token, const char *nodo_id, int n) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    char nbuf[16];
    pmcd_build_payload(payload, sizeof(payload), "token", token);
    pmcd_build_payload(payload, sizeof(payload), "nodo_id", nodo_id);
    pmcd_build_payload(payload, sizeof(payload), "consulta", "HISTORICO");
    snprintf(nbuf, sizeof(nbuf), "%d", n);
    pmcd_build_payload(payload, sizeof(payload), "n", nbuf);

    char resp[PMCD_MAX_PAYLOAD];
    int tipo = transaccion(host, puerto, PMCD_QUERY_REQ, payload,
                           resp, sizeof(resp));
    mostrar_respuesta(tipo, resp);
}

static void cmd_resumen(const char *host, const char *puerto,
                        const char *token) {
    char payload[PMCD_MAX_PAYLOAD] = "";
    pmcd_build_payload(payload, sizeof(payload), "token", token);
    pmcd_build_payload(payload, sizeof(payload), "consulta", "RESUMEN");

    char resp[PMCD_MAX_PAYLOAD];
    int tipo = transaccion(host, puerto, PMCD_QUERY_REQ, payload,
                           resp, sizeof(resp));
    mostrar_respuesta(tipo, resp);
}

/* Ejecuta un comando ya tokenizado (argv-style). Retorna 1 para continuar,
   0 para salir. */
static int ejecutar(const char *host, const char *puerto, const char *token,
                    char **tok, int ntok) {
    if (ntok == 0) {
        return 1;
    }
    if (strcmp(tok[0], "salir") == 0 || strcmp(tok[0], "exit") == 0) {
        return 0;
    }
    if (strcmp(tok[0], "estado") == 0 && ntok >= 2) {
        cmd_estado(host, puerto, token, tok[1]);
    } else if (strcmp(tok[0], "historico") == 0 && ntok >= 2) {
        int n = (ntok >= 3) ? atoi(tok[2]) : 5;
        if (n <= 0) n = 5;
        cmd_historico(host, puerto, token, tok[1], n);
    } else if (strcmp(tok[0], "resumen") == 0) {
        cmd_resumen(host, puerto, token);
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
    if (argc < 4) {
        fprintf(stderr,
                "Uso: %s <host> <puerto> <usuario> [comando...]\n"
                "  Comandos: estado <nodo_id> | historico <nodo_id> [n] | "
                "resumen\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];
    const char *puerto = argv[2];
    const char *usuario = argv[3];

    su_ignorar_sigpipe();

    char token[64] = "";
    if (autenticar(host, puerto, usuario, token, sizeof(token)) != 0) {
        return EXIT_FAILURE;
    }

    /* Modo comando unico: argv[4..] forman un comando. */
    if (argc > 4) {
        int r = ejecutar(host, puerto, token, &argv[4], argc - 4);
        return r >= 0 ? EXIT_SUCCESS : EXIT_FAILURE;
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
        if (!ejecutar(host, puerto, token, tok, ntok)) {
            break;
        }
    }

    printf("Cliente cerrado.\n");
    return EXIT_SUCCESS;
}
