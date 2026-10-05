/*
 * sesiones.c - Implementacion de la tabla de sesiones autenticadas.
 */

#include "sesiones.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    int    en_uso;
    char   token[SES_TOKEN_LEN];
    char   usuario[SES_USUARIO_LEN];
    char   rol[SES_ROL_LEN];
    time_t expira_en;
} sesion_t;

static sesion_t g_sesiones[SES_MAX];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void sesiones_init(void) {
    pthread_mutex_lock(&g_mutex);
    memset(g_sesiones, 0, sizeof(g_sesiones));
    pthread_mutex_unlock(&g_mutex);
}

/* Busca el indice de un token en uso. Requiere el mutex tomado. -1 si no. */
static int buscar_idx(const char *token) {
    for (int i = 0; i < SES_MAX; ++i) {
        if (g_sesiones[i].en_uso && strcmp(g_sesiones[i].token, token) == 0) {
            return i;
        }
    }
    return -1;
}

int sesiones_crear(const char *token, const char *usuario, const char *rol) {
    if (token == NULL || token[0] == '\0') {
        return -1;
    }

    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(token);
    if (idx < 0) {
        for (int i = 0; i < SES_MAX; ++i) {
            if (!g_sesiones[i].en_uso) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1; /* tabla llena */
    }

    g_sesiones[idx].en_uso = 1;
    snprintf(g_sesiones[idx].token, sizeof(g_sesiones[idx].token), "%s", token);
    snprintf(g_sesiones[idx].usuario, sizeof(g_sesiones[idx].usuario), "%s",
             (usuario != NULL) ? usuario : "");
    snprintf(g_sesiones[idx].rol, sizeof(g_sesiones[idx].rol), "%s",
             (rol != NULL) ? rol : "");
    g_sesiones[idx].expira_en = time(NULL) + SES_TTL_SEG;

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int sesiones_validar(const char *token, char *out_rol, size_t rol_cap) {
    if (token == NULL || token[0] == '\0') {
        return 0;
    }

    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(token);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return 0; /* inexistente */
    }

    if (time(NULL) >= g_sesiones[idx].expira_en) {
        /* Expirado: liberamos la entrada. */
        g_sesiones[idx].en_uso = 0;
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    if (out_rol != NULL && rol_cap > 0) {
        snprintf(out_rol, rol_cap, "%s", g_sesiones[idx].rol);
    }

    pthread_mutex_unlock(&g_mutex);
    return 1;
}

int sesiones_limpiar_expiradas(void) {
    int eliminadas = 0;
    time_t ahora = time(NULL);

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < SES_MAX; ++i) {
        if (g_sesiones[i].en_uso && ahora >= g_sesiones[i].expira_en) {
            g_sesiones[i].en_uso = 0;
            eliminadas++;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return eliminadas;
}
