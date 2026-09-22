#ifndef LOG_UTILS_H
#define LOG_UTILS_H

/*
 * log_utils.h - Logging del servidor PMCD.
 *
 * El servidor registra cada peticion entrante y cada respuesta enviada
 * simultaneamente en consola y en el archivo de logs indicado por parametro,
 * incluyendo un timestamp y el identificador del origen (IP:puerto). El
 * acceso al archivo esta protegido con un mutex porque varios hilos escriben.
 */

#include <stdio.h>

/* Direccion del mensaje registrado, para dar contexto en el log. */
typedef enum {
    LOG_PETICION,   /* algo que llego al servidor          */
    LOG_RESPUESTA,  /* algo que el servidor envio           */
    LOG_INFO        /* eventos internos (arranque, cierre)  */
} log_dir_t;

/*
 * log_abrir: abre (append) el archivo de logs 'ruta' e inicializa el mutex.
 * Retorna 0 en exito, -1 si no pudo abrir el archivo.
 */
int log_abrir(const char *ruta);

/*
 * log_evento: escribe una linea de log en consola y archivo.
 *
 *  dir       LOG_PETICION / LOG_RESPUESTA / LOG_INFO
 *  origen    identificador del cliente/nodo ("IP:puerto") o NULL/"" para
 *            eventos internos
 *  tipo_msg  nombre del tipo de mensaje PMCD (p.ej. "REG_REQ") o etiqueta
 *  detalle   texto libre adicional (payload, resultado, etc.); puede ser NULL
 *
 * Formato: [YYYY-MM-DD HH:MM:SS] <DIR> origen=IP:puerto tipo=REG_REQ detalle
 */
void log_evento(log_dir_t dir, const char *origen,
                const char *tipo_msg, const char *detalle);

/* log_cerrar: cierra el archivo y destruye el mutex. */
void log_cerrar(void);

#endif /* LOG_UTILS_H */
