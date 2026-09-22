/*
 * log_utils.c - Implementacion del logging del servidor.
 *
 * Escribe cada evento en stdout y en el archivo de logs, con timestamp y el
 * identificador de origen (IP:puerto). Un mutex serializa el acceso porque
 * el hilo TCP, el hilo UDP y el hilo principal pueden loguear a la vez.
 */

#include "log_utils.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_log = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

int log_abrir(const char *ruta) {
    if (ruta == NULL) {
        return -1;
    }
    g_log = fopen(ruta, "a");
    if (g_log == NULL) {
        perror("fopen(archivoDeLogs)");
        return -1;
    }
    return 0;
}

static const char *dir_texto(log_dir_t dir) {
    switch (dir) {
        case LOG_PETICION:  return "PETICION ";
        case LOG_RESPUESTA: return "RESPUESTA";
        case LOG_INFO:      return "INFO     ";
        default:            return "?        ";
    }
}

void log_evento(log_dir_t dir, const char *origen,
                const char *tipo_msg, const char *detalle) {
    char ts[32];
    time_t ahora = time(NULL);
    struct tm tm_buf;

    /* localtime_r es reentrante (varios hilos loguean). */
    localtime_r(&ahora, &tm_buf);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *org = (origen != NULL && origen[0] != '\0') ? origen : "-";
    const char *tip = (tipo_msg != NULL) ? tipo_msg : "-";
    const char *det = (detalle != NULL) ? detalle : "";

    pthread_mutex_lock(&g_log_mutex);

    /* Consola. */
    printf("[%s] %s origen=%s tipo=%s %s\n",
           ts, dir_texto(dir), org, tip, det);
    fflush(stdout);

    /* Archivo. */
    if (g_log != NULL) {
        fprintf(g_log, "[%s] %s origen=%s tipo=%s %s\n",
                ts, dir_texto(dir), org, tip, det);
        fflush(g_log);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

void log_cerrar(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log != NULL) {
        fclose(g_log);
        g_log = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}
