/*
 * socket_utils.c - Implementacion de las utilidades de sockets Berkeley.
 *
 * Toda resolucion de nombres pasa por getaddrinfo(); no hay direcciones IP
 * codificadas. Los fallos de resolucion se reportan y devuelven error, sin
 * terminar el proceso (requerimiento 4 del enunciado).
 *
 * Compilamos con -std=c11 (estricto); las extensiones POSIX necesarias
 * (getaddrinfo, struct addrinfo, getnameinfo, NI_*) se habilitan con
 * -D_POSIX_C_SOURCE=200112L en el Makefile.
 */

#include "socket_utils.h"

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void su_ignorar_sigpipe(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal(SIGPIPE)");
    }
}

void su_close(int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

/* ------------------------------------------------------------------ */
/* Servidor                                                            */
/* ------------------------------------------------------------------ */

int su_crear_socket_servidor_tcp(const char *puerto, int backlog) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    int sockfd = -1;
    int gai;

    if (puerto == NULL) {
        fprintf(stderr, "su_crear_socket_servidor_tcp: puerto NULL\n");
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* IPv4 (consistente con la Fase 1) */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */
    hints.ai_flags    = AI_PASSIVE;   /* para bind en INADDR_ANY */

    gai = getaddrinfo(NULL, puerto, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo(TCP, puerto=%s): %s\n",
                puerto, gai_strerror(gai));
        return -1; /* no termina el proceso */
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            perror("socket");
            continue;
        }

        int reuse = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) < 0) {
            perror("bind(TCP)");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        if (listen(sockfd, backlog) < 0) {
            perror("listen");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        break; /* socket listo */
    }

    freeaddrinfo(res);

    if (sockfd < 0) {
        fprintf(stderr, "No se pudo crear el socket TCP de escucha.\n");
    }
    return sockfd;
}

int su_crear_socket_servidor_udp(const char *puerto) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    int sockfd = -1;
    int gai;

    if (puerto == NULL) {
        fprintf(stderr, "su_crear_socket_servidor_udp: puerto NULL\n");
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;   /* UDP */
    hints.ai_flags    = AI_PASSIVE;

    gai = getaddrinfo(NULL, puerto, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo(UDP, puerto=%s): %s\n",
                puerto, gai_strerror(gai));
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            perror("socket(UDP)");
            continue;
        }

        int reuse = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) < 0) {
            perror("bind(UDP)");
            close(sockfd);
            sockfd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo(res);

    if (sockfd < 0) {
        fprintf(stderr, "No se pudo crear el socket UDP.\n");
    }
    return sockfd;
}

/* ------------------------------------------------------------------ */
/* Cliente / nodo                                                      */
/* ------------------------------------------------------------------ */

int su_conectar_cliente_tcp(const char *host, const char *puerto) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    int sockfd = -1;
    int gai;

    if (host == NULL || puerto == NULL) {
        fprintf(stderr, "su_conectar_cliente_tcp: host/puerto NULL\n");
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    gai = getaddrinfo(host, puerto, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo(%s:%s): %s\n",
                host, puerto, gai_strerror(gai));
        return -1; /* fallo de resolucion: no termina el proceso */
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; /* conectado */
        }
        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);

    if (sockfd < 0) {
        fprintf(stderr, "No se pudo conectar a %s:%s (%s)\n",
                host, puerto, strerror(errno));
    }
    return sockfd;
}

int su_crear_socket_udp_cliente(void) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket(UDP cliente)");
    }
    return sockfd;
}

int su_resolver_destino_udp(const char *host, const char *puerto,
                            struct sockaddr_storage *out_addr,
                            socklen_t *out_len) {
    struct addrinfo hints, *res = NULL;
    int gai;

    if (host == NULL || puerto == NULL || out_addr == NULL || out_len == NULL) {
        fprintf(stderr, "su_resolver_destino_udp: argumentos NULL\n");
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    gai = getaddrinfo(host, puerto, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo(UDP %s:%s): %s\n",
                host, puerto, gai_strerror(gai));
        return -1;
    }

    if (res == NULL) {
        fprintf(stderr, "getaddrinfo(UDP %s:%s): sin direcciones\n",
                host, puerto);
        return -1;
    }

    memcpy(out_addr, res->ai_addr, res->ai_addrlen);
    *out_len = res->ai_addrlen;

    freeaddrinfo(res);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Envio / recepcion                                                   */
/* ------------------------------------------------------------------ */

int su_send_all(int sockfd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t enviados = 0;

    while (enviados < len) {
        ssize_t r = send(sockfd, p + enviados, len - enviados, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return -1;
        }
        if (r == 0) {
            return -1; /* conexion cerrada */
        }
        enviados += (size_t)r;
    }
    return 0;
}

/* Lee exactamente 'len' bytes. Retorna 1 completo, 0 cierre, -1 error. */
static int recv_exacto(int sockfd, uint8_t *buf, size_t len) {
    size_t recibidos = 0;
    while (recibidos < len) {
        ssize_t r = recv(sockfd, buf + recibidos, len - recibidos, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            return -1;
        }
        if (r == 0) {
            return 0; /* el otro extremo cerro */
        }
        recibidos += (size_t)r;
    }
    return 1;
}

int su_recv_pmcd_msg(int sockfd, pmcd_header_t *h,
                     char *out_payload, size_t payload_cap) {
    uint8_t buf[PMCD_MAX_MSG];
    int r;

    /* 1. Encabezado. */
    r = recv_exacto(sockfd, buf, PMCD_HEADER_SIZE);
    if (r <= 0) {
        return r; /* 0 cierre, -1 error */
    }

    /* 2. Desempaquetar solo el encabezado para conocer PAYLOAD_LEN. */
    pmcd_header_t tmp;
    int pr = pmcd_unpack(buf, PMCD_HEADER_SIZE, &tmp, NULL, 0);
    if (pr != PMCD_OK && pr != PMCD_ERR_PAYLOAD_LEN) {
        /* MAGIC/VERSION invalidos: mensaje corrupto. */
        return -2;
    }
    /* pmcd_unpack devuelve PMCD_ERR_PAYLOAD_LEN porque aun no leimos el
       payload; validamos MAGIC/VERSION reintentando la lectura de esos
       campos ya cargados en tmp. */
    if (tmp.magic[0] != PMCD_MAGIC_0 || tmp.magic[1] != PMCD_MAGIC_1 ||
        tmp.version != PMCD_VERSION) {
        return -2;
    }
    if (tmp.payload_len > PMCD_MAX_PAYLOAD) {
        return -2;
    }

    /* 3. Payload. */
    if (tmp.payload_len > 0) {
        r = recv_exacto(sockfd, buf + PMCD_HEADER_SIZE, tmp.payload_len);
        if (r <= 0) {
            return r;
        }
    }

    /* 4. Desempaquetado completo con validacion. */
    pr = pmcd_unpack(buf, (size_t)PMCD_HEADER_SIZE + tmp.payload_len,
                     h, out_payload, payload_cap);
    if (pr != PMCD_OK) {
        return -2;
    }
    return 1;
}

void su_endpoint_str(const struct sockaddr *addr, socklen_t addrlen,
                     char *out, size_t out_cap) {
    char host[64];
    char serv[16];

    if (out == NULL || out_cap == 0) {
        return;
    }

    int gai = getnameinfo(addr, addrlen, host, sizeof(host),
                          serv, sizeof(serv),
                          NI_NUMERICHOST | NI_NUMERICSERV);
    if (gai != 0) {
        snprintf(out, out_cap, "?:?");
        return;
    }
    snprintf(out, out_cap, "%s:%s", host, serv);
}
