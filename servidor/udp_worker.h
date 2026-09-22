#ifndef UDP_WORKER_H
#define UDP_WORKER_H

/*
 * udp_worker.h - Hilo dedicado a la recepcion de datagramas UDP
 * (TELEMETRY y HEARTBEAT) en el servidor.
 *
 * Ejecuta recvfrom() en bucle sobre el socket UDP, desempaqueta cada
 * datagrama PMCD, actualiza la tabla de estado / historico (bajo el mutex
 * del modulo 'estado') y registra la actividad en el log. Ante un datagrama
 * con formato invalido, responde ERROR por el mismo socket UDP al remitente.
 */

/* Argumento que recibe el hilo. */
typedef struct {
    int  udp_fd;         /* socket UDP ya enlazado          */
    volatile int *parar; /* bandera: !=0 para terminar      */
} udp_worker_args_t;

/* Funcion de hilo (pthread_create). Recibe un udp_worker_args_t*. */
void *udp_worker(void *arg);

#endif /* UDP_WORKER_H */
