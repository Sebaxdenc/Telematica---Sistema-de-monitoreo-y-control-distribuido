/*
 * monitor.c - Implementacion del hilo de deteccion de inactividad.
 *
 * Cada MON_PERIODO segundos llama a estado_revisar_inactividad(), que aplica
 * las transiciones ACTIVO->INACTIVO->baja. Cada cambio se registra en el log
 * a traves del callback 'on_cambio'.
 */

#include "monitor.h"
#include "estado.h"

#include "log_utils.h"

#include <stdio.h>
#include <unistd.h>

/* Callback invocado por estado_revisar_inactividad ante cada transicion. */
static void on_cambio(const char *id, nodo_estado_t nuevo, long inactivo_seg) {
    char detalle[128];
    if (nuevo == NODO_INACTIVO) {
        snprintf(detalle, sizeof(detalle),
                 "nodo=%s -> INACTIVO (sin senal %ld s)", id, inactivo_seg);
    } else { /* NODO_DESCONECTADO = baja */
        snprintf(detalle, sizeof(detalle),
                 "nodo=%s -> BAJA (sin senal %ld s, exige re-registro)",
                 id, inactivo_seg);
    }
    log_evento(LOG_INFO, NULL, "MONITOR", detalle);
}

void *monitor_worker(void *arg) {
    monitor_args_t *a = (monitor_args_t *)arg;

    while (!*(a->parar)) {
        /* Dormir en tramos de 1s para reaccionar rapido a la parada. */
        for (int i = 0; i < MON_PERIODO && !*(a->parar); ++i) {
            sleep(1);
        }
        if (*(a->parar)) {
            break;
        }
        estado_revisar_inactividad(MON_T_INACTIVO, MON_T_BAJA, on_cambio);
    }

    return NULL;
}
