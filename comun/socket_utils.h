#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

/*
 * socket_utils.h - Utilidades de sockets Berkeley para PMCD/1.0.
 *
 * Encapsula la creacion de sockets TCP/UDP resolviendo nombres con
 * getaddrinfo() (nunca IPs codificadas, requerimiento 4 del enunciado) y
 * el envio/recepcion de mensajes PMCD completos sobre TCP (manejo de
 * lecturas/escrituras parciales).
 *
 * Compilar en WSL/Linux (headers POSIX).
 */

#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "pmcd_protocol.h"

/* Longitud de puerto como cadena ("0".."65535" + '\0'). */
#define SU_PORT_STRLEN 6
/* Longitud comoda para "IP:puerto" en texto. */
#define SU_ENDPOINT_STRLEN 64

/*
 * su_ignorar_sigpipe: ignora SIGPIPE para que un send() sobre una conexion
 * cerrada por el otro extremo no termine el proceso (se maneja via el valor
 * de retorno de send()). Llamar una vez al inicio del programa.
 */
void su_ignorar_sigpipe(void);

/* ------------------------------------------------------------------ */
/* Servidor                                                            */
/* ------------------------------------------------------------------ */

/*
 * su_crear_socket_servidor_tcp: crea un socket TCP de escucha en 'puerto'
 * (cadena, p.ej. "5000") usando getaddrinfo con AI_PASSIVE, SO_REUSEADDR,
 * bind y listen(backlog).
 *
 * Retorna el descriptor (>=0) o -1 en error (ya reportado con perror /
 * mensaje). No termina el proceso: el llamador decide.
 */
int su_crear_socket_servidor_tcp(const char *puerto, int backlog);

/*
 * su_crear_socket_servidor_udp: crea un socket UDP enlazado a 'puerto'
 * usando getaddrinfo con AI_PASSIVE y bind (sin listen).
 *
 * Retorna el descriptor (>=0) o -1 en error.
 */
int su_crear_socket_servidor_udp(const char *puerto);

/* ------------------------------------------------------------------ */
/* Cliente / nodo                                                      */
/* ------------------------------------------------------------------ */

/*
 * su_conectar_cliente_tcp: resuelve 'host' + 'puerto' con getaddrinfo y
 * abre una conexion TCP, probando cada direccion devuelta hasta que una
 * conecte.
 *
 * Retorna el descriptor conectado (>=0) o -1 en error (incluido fallo de
 * resolucion, que se reporta sin terminar el proceso).
 */
int su_conectar_cliente_tcp(const char *host, const char *puerto);

/*
 * su_crear_socket_udp_cliente: crea un socket UDP no enlazado para enviar
 * datagramas. Retorna el descriptor o -1.
 */
int su_crear_socket_udp_cliente(void);

/*
 * su_resolver_destino_udp: resuelve 'host' + 'puerto' con getaddrinfo y
 * copia la primera direccion valida en 'out_addr' (para usar luego con
 * sendto). Escribe la longitud en 'out_len'.
 *
 * Retorna 0 si resolvio, -1 en error (reportado, sin terminar el proceso).
 */
int su_resolver_destino_udp(const char *host, const char *puerto,
                            struct sockaddr_storage *out_addr,
                            socklen_t *out_len);

/* ------------------------------------------------------------------ */
/* Envio / recepcion                                                   */
/* ------------------------------------------------------------------ */

/*
 * su_send_all: envia exactamente 'len' bytes por un socket TCP, repitiendo
 * send() ante envios parciales y reintentando en EINTR.
 *
 * Retorna 0 si envio todo, -1 en error o si el otro extremo cerro.
 */
int su_send_all(int sockfd, const void *buf, size_t len);

/*
 * su_recv_pmcd_msg: recibe un mensaje PMCD completo de un socket TCP: lee
 * primero los PMCD_HEADER_SIZE bytes del encabezado, interpreta
 * PAYLOAD_LEN y luego lee ese numero de bytes de payload, manejando
 * lecturas parciales.
 *
 *  sockfd        socket TCP conectado
 *  h             [salida] encabezado desempaquetado y validado
 *  out_payload   [salida] payload como cadena (terminada en '\0')
 *  payload_cap   capacidad de out_payload
 *
 * Retorna:
 *    1  mensaje recibido correctamente
 *    0  el otro extremo cerro la conexion antes de completar el mensaje
 *   -1  error de red
 *   -2  mensaje PMCD invalido (MAGIC/VERSION/PAYLOAD_LEN)
 */
int su_recv_pmcd_msg(int sockfd, pmcd_header_t *h,
                     char *out_payload, size_t payload_cap);

/*
 * su_endpoint_str: formatea la direccion 'addr' como "IP:puerto" en 'out'
 * (usa getnameinfo con NI_NUMERICHOST/NI_NUMERICSERV). Util para logs.
 */
void su_endpoint_str(const struct sockaddr *addr, socklen_t addrlen,
                     char *out, size_t out_cap);

/* Cierra un socket ignorando el resultado (conveniencia). */
void su_close(int sockfd);

#endif /* SOCKET_UTILS_H */
