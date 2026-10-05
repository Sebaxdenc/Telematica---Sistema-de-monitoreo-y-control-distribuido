#ifndef MONITOR_H
#define MONITOR_H

/*
 * monitor.h - Hilo "barrendero" que detecta nodos inactivos por temporizador.
 *
 * Periodicamente revisa la tabla de estado y aplica las transiciones de la
 * maquina de estados del nodo (Fase 1):
 *
 *   ACTIVO   --(sin senal > MON_T_INACTIVO seg)-->  INACTIVO
 *   INACTIVO --(sin senal > MON_T_BAJA seg)-->      baja (se exige re-registro)
 *
 * Esto resuelve el caso en que un nodo se apaga: deja de enviar telemetria y,
 * pasado el umbral, el servidor lo marca INACTIVO y luego lo da de baja.
 */

/* Umbrales (segundos) y periodo de revision. Ajustables. */
#define MON_T_INACTIVO 10   /* ACTIVO -> INACTIVO */
#define MON_T_BAJA     30   /* INACTIVO -> baja   */
#define MON_PERIODO    2    /* cada cuanto revisa la tabla */

/* Argumento del hilo. */
typedef struct {
    volatile int *parar;   /* !=0 para terminar el hilo */
} monitor_args_t;

/* Funcion de hilo (pthread_create). Recibe un monitor_args_t*. */
void *monitor_worker(void *arg);

#endif /* MONITOR_H */
