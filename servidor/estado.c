/*
 * estado.c - Implementacion del estado consolidado (tabla de nodos +
 * historico de telemetria), protegido por un mutex global.
 */

#include "estado.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static nodo_t g_nodos[EST_MAX_NODOS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void estado_init(void) {
    pthread_mutex_lock(&g_mutex);
    memset(g_nodos, 0, sizeof(g_nodos));
    pthread_mutex_unlock(&g_mutex);
}

void estado_destruir(void) {
    /* g_mutex es estatico con inicializador; no requiere destroy explicito,
       pero dejamos la funcion por simetria y posibles extensiones. */
}

const char *estado_nombre(nodo_estado_t e) {
    switch (e) {
        case NODO_ACTIVO:       return "ACTIVO";
        case NODO_INACTIVO:     return "INACTIVO";
        case NODO_DESCONECTADO: return "DESCONECTADO";
        default:                return "?";
    }
}

/* Busca el indice de un nodo por id. Requiere el mutex tomado. Retorna
   indice o -1. */
static int buscar_idx(const char *id) {
    for (int i = 0; i < EST_MAX_NODOS; ++i) {
        if (g_nodos[i].en_uso && strcmp(g_nodos[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Primera entrada libre, o -1 si no hay. Requiere el mutex tomado. */
static int primera_libre(void) {
    for (int i = 0; i < EST_MAX_NODOS; ++i) {
        if (!g_nodos[i].en_uso) {
            return i;
        }
    }
    return -1;
}

int estado_registrar_nodo(const char *id, const char *tipo,
                          const char *endpoint) {
    if (id == NULL || id[0] == '\0') {
        return -2;
    }

    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(id);
    if (idx < 0) {
        idx = primera_libre();
        if (idx < 0) {
            pthread_mutex_unlock(&g_mutex);
            return -1; /* tabla llena */
        }
        memset(&g_nodos[idx], 0, sizeof(g_nodos[idx]));
        g_nodos[idx].en_uso = 1;
        g_nodos[idx].registrado_en = time(NULL);
    }

    nodo_t *n = &g_nodos[idx];
    snprintf(n->id, sizeof(n->id), "%s", id);
    snprintf(n->tipo, sizeof(n->tipo), "%s", (tipo != NULL) ? tipo : "");
    snprintf(n->endpoint, sizeof(n->endpoint), "%s",
             (endpoint != NULL) ? endpoint : "");
    n->estado = NODO_ACTIVO;
    n->ultima_actividad = time(NULL);

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int estado_nodo_registrado(const char *id) {
    if (id == NULL) {
        return 0;
    }
    pthread_mutex_lock(&g_mutex);
    int idx = buscar_idx(id);
    pthread_mutex_unlock(&g_mutex);
    return idx >= 0 ? 1 : 0;
}

int estado_agregar_muestra(const char *id, const char *payload) {
    if (id == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    nodo_t *n = &g_nodos[idx];
    int pos;
    if (n->hist_cuenta < EST_HIST_POR_NODO) {
        pos = (n->hist_inicio + n->hist_cuenta) % EST_HIST_POR_NODO;
        n->hist_cuenta++;
    } else {
        /* Buffer lleno: sobrescribimos la mas antigua y avanzamos inicio. */
        pos = n->hist_inicio;
        n->hist_inicio = (n->hist_inicio + 1) % EST_HIST_POR_NODO;
    }

    n->muestras[pos].recibido_en = time(NULL);
    snprintf(n->muestras[pos].payload, sizeof(n->muestras[pos].payload),
             "%s", (payload != NULL) ? payload : "");

    n->ultima_actividad = time(NULL);
    n->estado = NODO_ACTIVO;

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int estado_marcar_actividad(const char *id) {
    if (id == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mutex);
    int idx = buscar_idx(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }
    g_nodos[idx].ultima_actividad = time(NULL);
    g_nodos[idx].estado = NODO_ACTIVO;
    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int estado_consulta_instantanea(const char *id, char *out, size_t out_cap) {
    if (id == NULL || out == NULL || out_cap == 0) {
        return -1;
    }
    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    nodo_t *n = &g_nodos[idx];
    const char *ultima = "(sin telemetria)";
    if (n->hist_cuenta > 0) {
        int last = (n->hist_inicio + n->hist_cuenta - 1) % EST_HIST_POR_NODO;
        ultima = n->muestras[last].payload;
    }

    snprintf(out, out_cap,
             "nodo_id=%s;tipo=%s;estado=%s;endpoint=%s;ultima=%s",
             n->id, n->tipo, estado_nombre(n->estado), n->endpoint, ultima);

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

int estado_consulta_historico(const char *id, int n_pedidas,
                              char *out, size_t out_cap) {
    if (id == NULL || out == NULL || out_cap == 0) {
        return -1;
    }
    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    nodo_t *n = &g_nodos[idx];
    int disponibles = n->hist_cuenta;
    int a_mostrar = (n_pedidas > 0 && n_pedidas < disponibles)
                        ? n_pedidas : disponibles;

    out[0] = '\0';
    size_t usado = 0;
    int escritas = 0;

    /* De la mas reciente a la mas antigua. */
    for (int k = 0; k < a_mostrar; ++k) {
        int pos = (n->hist_inicio + n->hist_cuenta - 1 - k + EST_HIST_POR_NODO)
                  % EST_HIST_POR_NODO;
        char linea[EST_PAYLOAD_LEN + 64];
        int w = snprintf(linea, sizeof(linea), "#%d ts=%ld %s\n",
                         k + 1, (long)n->muestras[pos].recibido_en,
                         n->muestras[pos].payload);
        if (w < 0) {
            break;
        }
        if (usado + (size_t)w >= out_cap) {
            break; /* no cabe mas */
        }
        memcpy(out + usado, linea, (size_t)w);
        usado += (size_t)w;
        out[usado] = '\0';
        escritas++;
    }

    pthread_mutex_unlock(&g_mutex);
    return escritas;
}

int estado_resumen(char *out, size_t out_cap) {
    if (out == NULL || out_cap == 0) {
        return 0;
    }
    pthread_mutex_lock(&g_mutex);

    out[0] = '\0';
    size_t usado = 0;
    int cuenta = 0;

    for (int i = 0; i < EST_MAX_NODOS; ++i) {
        if (!g_nodos[i].en_uso) {
            continue;
        }
        char linea[128];
        int w = snprintf(linea, sizeof(linea), "%s:%s;",
                         g_nodos[i].id, estado_nombre(g_nodos[i].estado));
        if (w < 0 || usado + (size_t)w >= out_cap) {
            break;
        }
        memcpy(out + usado, linea, (size_t)w);
        usado += (size_t)w;
        out[usado] = '\0';
        cuenta++;
    }

    pthread_mutex_unlock(&g_mutex);
    return cuenta;
}

int estado_revisar_inactividad(long t_inactivo_seg, long t_baja_seg,
                               estado_cambio_cb cb) {
    int cambios = 0;
    time_t ahora = time(NULL);

    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < EST_MAX_NODOS; ++i) {
        if (!g_nodos[i].en_uso) {
            continue;
        }
        nodo_t *n = &g_nodos[i];
        long inactivo = (long)(ahora - n->ultima_actividad);

        /* ACTIVO/INACTIVO sin senal por mas de t_baja_seg -> baja. */
        if (inactivo > t_baja_seg &&
            (n->estado == NODO_ACTIVO || n->estado == NODO_INACTIVO)) {
            /* Copiamos el id antes de liberar para el callback. */
            char id_copia[EST_ID_LEN];
            snprintf(id_copia, sizeof(id_copia), "%s", n->id);
            n->en_uso = 0;            /* dar de baja: libera la entrada */
            n->estado = NODO_DESCONECTADO;
            cambios++;
            if (cb != NULL) {
                cb(id_copia, NODO_DESCONECTADO, inactivo);
            }
            continue;
        }

        /* ACTIVO sin senal por mas de t_inactivo_seg -> INACTIVO. */
        if (inactivo > t_inactivo_seg && n->estado == NODO_ACTIVO) {
            n->estado = NODO_INACTIVO;
            cambios++;
            if (cb != NULL) {
                cb(n->id, NODO_INACTIVO, inactivo);
            }
        }
    }

    pthread_mutex_unlock(&g_mutex);
    return cambios;
}

int estado_chequear_seq(const char *id, unsigned int seq) {
    if (id == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mutex);

    int idx = buscar_idx(id);
    if (idx < 0) {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    nodo_t *n = &g_nodos[idx];

    if (!n->tiene_seq_udp) {
        /* Primer datagrama del nodo: se acepta y se fija la referencia. */
        n->tiene_seq_udp = 1;
        n->ultimo_seq_udp = seq;
        pthread_mutex_unlock(&g_mutex);
        return 1;
    }

    if (seq > n->ultimo_seq_udp) {
        n->ultimo_seq_udp = seq;
        pthread_mutex_unlock(&g_mutex);
        return 1; /* en orden y nuevo */
    }

    /* seq <= ultimo aceptado: duplicado o fuera de orden. */
    pthread_mutex_unlock(&g_mutex);
    return 0;
}
