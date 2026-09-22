/*
 * servidor.c - Servidor central del Sistema de Monitoreo y Control
 * Distribuido (PMCD/1.0).
 *
 * Uso:
 *     ./servidor <puerto> <archivoDeLogs>
 *
 *   puerto         puerto por el que recibe peticiones TCP y datagramas UDP.
 *   archivoDeLogs  archivo donde se almacenan peticiones y respuestas.
 *
 * Concurrencia (Task 8): un hilo dedicado atiende la recepcion UDP y cada
 * conexion TCP se atiende en su propio hilo (detached), con un limite de
 * MAX_CONNECTIONS conexiones activas controlado por mutex + variable de
 * condicion (mismo patron que el ejemplo c_sockets_estudio de EAFIT). La
 * tabla de estado es compartida y se protege dentro del modulo 'estado'.
 *
 * Cierre ordenado: SIGINT/SIGTERM activan una bandera; el bucle de aceptacion
 * termina, se detiene el hilo UDP, se cierran los sockets y se cierra el log.
 */

#include "estado.h"
#include "manejador.h"
#include "udp_worker.h"

#include "log_utils.h"
#include "pmcd_protocol.h"
#include "socket_utils.h"

#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKLOG         16
#define MAX_CONNECTIONS 16   /* conexiones TCP atendidas simultaneamente */

/* Bandera de parada activada por senal (async-signal-safe). */
static volatile sig_atomic_t g_parar = 0;

/* Control de conexiones TCP activas. */
static int g_activas = 0;
static pthread_mutex_t g_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_conn_cond  = PTHREAD_COND_INITIALIZER;

/* Argumento para el hilo de conexion TCP. */
typedef struct {
    int  fd;
    char endpoint[SU_ENDPOINT_STRLEN];
} conn_arg_t;

static void manejar_senal(int sig) {
    (void)sig;
    g_parar = 1;
}

static void liberar_cupo(void) {
    pthread_mutex_lock(&g_conn_mutex);
    g_activas--;
    pthread_cond_signal(&g_conn_cond);
    pthread_mutex_unlock(&g_conn_mutex);
}

/* Hilo que atiende una conexion TCP. */
static void *hilo_conexion(void *arg) {
    conn_arg_t *c = (conn_arg_t *)arg;
    manejar_conexion_tcp(c->fd, c->endpoint); /* cierra c->fd al terminar */
    free(c);
    liberar_cupo();
    return NULL;
}

static void uso(const char *prog) {
    fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }

    const char *puerto = argv[1];
    const char *archivo_logs = argv[2];

    char *fin = NULL;
    long p = strtol(puerto, &fin, 10);
    if (fin == puerto || *fin != '\0' || p < 1 || p > 65535) {
        fprintf(stderr, "Puerto invalido: '%s' (use 1..65535)\n", puerto);
        return EXIT_FAILURE;
    }

    su_ignorar_sigpipe();
    estado_init();

    /* Manejo de senales para cierre ordenado. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_senal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (log_abrir(archivo_logs) != 0) {
        fprintf(stderr, "No se pudo abrir el archivo de logs '%s'.\n",
                archivo_logs);
        estado_destruir();
        return EXIT_FAILURE;
    }

    int tcp_fd = su_crear_socket_servidor_tcp(puerto, BACKLOG);
    if (tcp_fd < 0) {
        log_evento(LOG_INFO, NULL, "ARRANQUE", "fallo al crear socket TCP");
        log_cerrar();
        estado_destruir();
        return EXIT_FAILURE;
    }

    int udp_fd = su_crear_socket_servidor_udp(puerto);
    if (udp_fd < 0) {
        log_evento(LOG_INFO, NULL, "ARRANQUE", "fallo al crear socket UDP");
        su_close(tcp_fd);
        log_cerrar();
        estado_destruir();
        return EXIT_FAILURE;
    }

    {
        char detalle[160];
        snprintf(detalle, sizeof(detalle),
                 "servidor PMCD/1.0 en puerto %s (TCP+UDP), max %d conexiones, logs='%s'",
                 puerto, MAX_CONNECTIONS, archivo_logs);
        log_evento(LOG_INFO, NULL, "ARRANQUE", detalle);
    }
    printf("Servidor PMCD/1.0 listo en puerto %s (TCP+UDP, hasta %d conexiones). "
           "Ctrl+C para detener.\n", puerto, MAX_CONNECTIONS);

    /* Hilo dedicado a la recepcion UDP (telemetria / heartbeat). */
    volatile int parar_udp = 0;
    udp_worker_args_t udp_args = { .udp_fd = udp_fd, .parar = &parar_udp };
    pthread_t udp_th;
    int udp_thr = pthread_create(&udp_th, NULL, udp_worker, &udp_args);
    if (udp_thr != 0) {
        log_evento(LOG_INFO, NULL, "ARRANQUE", "no se pudo crear el hilo UDP");
        su_close(tcp_fd);
        su_close(udp_fd);
        log_cerrar();
        estado_destruir();
        return EXIT_FAILURE;
    }

    /* Hilos de conexion TCP en modo detached. */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* Bucle de aceptacion concurrente. */
    while (!g_parar) {
        /* Esperar a que haya cupo (o a que pidan parar). */
        pthread_mutex_lock(&g_conn_mutex);
        while (g_activas >= MAX_CONNECTIONS && !g_parar) {
            pthread_cond_wait(&g_conn_cond, &g_conn_mutex);
        }
        if (g_parar) {
            pthread_mutex_unlock(&g_conn_mutex);
            break;
        }
        g_activas++;
        pthread_mutex_unlock(&g_conn_mutex);

        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = accept(tcp_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) {
            liberar_cupo();
            if (errno == EINTR) {
                continue; /* posible senal: reevaluar g_parar */
            }
            perror("accept");
            continue;
        }

        conn_arg_t *c = malloc(sizeof(*c));
        if (c == NULL) {
            su_close(cli_fd);
            liberar_cupo();
            continue;
        }
        c->fd = cli_fd;
        su_endpoint_str((struct sockaddr *)&cli_addr, cli_len,
                        c->endpoint, sizeof(c->endpoint));

        log_evento(LOG_INFO, c->endpoint, "CONEXION", "conexion TCP aceptada");

        pthread_t th;
        int tr = pthread_create(&th, &attr, hilo_conexion, c);
        if (tr != 0) {
            log_evento(LOG_INFO, c->endpoint, "CONEXION",
                       "no se pudo crear hilo de atencion");
            su_close(c->fd);
            free(c);
            liberar_cupo();
        }
    }

    /* --- Cierre ordenado --- */
    log_evento(LOG_INFO, NULL, "CIERRE", "senal recibida, deteniendo servidor");

    /* Detener el hilo UDP y esperarlo. */
    parar_udp = 1;
    pthread_join(udp_th, NULL);

    pthread_attr_destroy(&attr);
    su_close(tcp_fd);
    su_close(udp_fd);

    log_evento(LOG_INFO, NULL, "CIERRE", "servidor detenido");
    log_cerrar();
    estado_destruir();
    return EXIT_SUCCESS;
}
