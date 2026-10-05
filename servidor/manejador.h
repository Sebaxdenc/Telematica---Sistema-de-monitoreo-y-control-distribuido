#ifndef MANEJADOR_H
#define MANEJADOR_H

/*
 * manejador.h - Logica de atencion de una conexion TCP entrante.
 *
 * Aisla el procesamiento del protocolo (interpretar mensajes, actualizar el
 * estado y construir respuestas) para poder invocarlo tanto desde el bucle
 * secuencial (Task 5) como desde un hilo por conexion (Task 8).
 */

/*
 * manejador_config_auth: fija la ubicacion del servicio de autenticacion
 * (host y puerto) que el manejador usara al procesar AUTH_REQ. Debe llamarse
 * una vez al arrancar el servidor, antes de aceptar conexiones.
 */
void manejador_config_auth(const char *auth_host, const char *auth_puerto);

/*
 * manejar_conexion_tcp: atiende una conexion TCP ya aceptada. Lee mensajes
 * del socket, los interpreta segun PMCD/1.0, actualiza el estado y envia las
 * respuestas correspondientes. Registra cada peticion y respuesta en el log
 * con el identificador de origen 'endpoint' (IP:puerto).
 *
 * Cierra 'cli_fd' antes de retornar.
 *
 *  cli_fd     descriptor de la conexion aceptada
 *  endpoint   "IP:puerto" del cliente/nodo (para el log)
 */
void manejar_conexion_tcp(int cli_fd, const char *endpoint);

#endif /* MANEJADOR_H */
