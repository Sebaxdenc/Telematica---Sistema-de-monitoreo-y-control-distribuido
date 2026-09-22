#ifndef PMCD_PROTOCOL_H
#define PMCD_PROTOCOL_H

/*
 * pmcd_protocol.h - Protocolo de Monitoreo y Control Distribuido (PMCD/1.0)
 *
 * Define el vocabulario de mensajes, el encabezado binario de tamano fijo
 * (16 bytes) y las funciones de empaquetado/desempaquetado y de manejo del
 * payload de texto (pares campo=valor;).
 *
 * Diseno de referencia: Fase 1 (Fase1_Diseno_Arquitectura_PMCD.md).
 */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constantes del protocolo                                            */
/* ------------------------------------------------------------------ */

#define PMCD_MAGIC_0 0x50 /* 'P' */
#define PMCD_MAGIC_1 0x4D /* 'M' */
#define PMCD_VERSION 0x01

/* Tamano del encabezado en el cable (bytes). */
#define PMCD_HEADER_SIZE 16

/* Tamano maximo del payload. PAYLOAD_LEN es de 2 bytes -> hasta 65535,
   pero limitamos a un valor comodo para los buffers de la aplicacion. */
#define PMCD_MAX_PAYLOAD 4096

/* Tamano maximo de un mensaje completo (encabezado + payload). */
#define PMCD_MAX_MSG (PMCD_HEADER_SIZE + PMCD_MAX_PAYLOAD)

/* ------------------------------------------------------------------ */
/* Tipos de mensaje (campo TYPE)                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    PMCD_REG_REQ    = 0x01, /* Nodo   -> Servidor : registro                 */
    PMCD_REG_RESP   = 0x02, /* Servidor -> Nodo   : confirmacion de registro */
    PMCD_AUTH_REQ   = 0x03, /* Cliente -> Servidor: autenticacion            */
    PMCD_AUTH_RESP  = 0x04, /* Servidor -> Cliente: token o error            */
    PMCD_TELEMETRY  = 0x05, /* Nodo   -> Servidor : telemetria (UDP)         */
    PMCD_EVENT      = 0x06, /* Nodo   -> Servidor : evento critico           */
    PMCD_EVENT_ACK  = 0x07, /* Servidor -> Nodo   : ack de evento            */
    PMCD_QUERY_REQ  = 0x08, /* Cliente -> Servidor: consulta                 */
    PMCD_QUERY_RESP = 0x09, /* Servidor -> Cliente: respuesta a consulta     */
    PMCD_HEARTBEAT  = 0x0A, /* Nodo   -> Servidor : senal de vida (UDP)      */
    PMCD_ERROR      = 0x0B, /* Servidor -> ...    : reporte de error         */
    PMCD_DISCONNECT = 0x0C  /* Nodo/Cliente -> Servidor: cierre de sesion    */
} pmcd_tipo_t;

/* ------------------------------------------------------------------ */
/* Flags (campo FLAGS, mapa de bits)                                   */
/* ------------------------------------------------------------------ */

#define PMCD_FLAG_REQUIERE_ACK 0x01 /* bit 0: el emisor espera confirmacion  */
#define PMCD_FLAG_ES_RESPUESTA 0x02 /* bit 1: responde a otro (mismo SEQ_NUM) */
#define PMCD_FLAG_REENVIO      0x04 /* bit 2: reintento de un SEQ_NUM previo  */

/* ------------------------------------------------------------------ */
/* Encabezado PMCD en representacion de host                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  magic[2];     /* 'P','M'                                        */
    uint8_t  version;      /* PMCD_VERSION                                   */
    uint8_t  tipo;         /* pmcd_tipo_t                                    */
    uint8_t  flags;        /* combinacion de PMCD_FLAG_*                     */
    uint32_t seq_num;      /* numero de secuencia del emisor                 */
    uint32_t timestamp;    /* marca de tiempo Unix del envio                 */
    uint16_t payload_len;  /* longitud del payload en bytes                  */
    uint8_t  reservado;    /* alineacion / uso futuro                        */
} pmcd_header_t;

/* Codigos de resultado de las funciones del protocolo. */
typedef enum {
    PMCD_OK                 =  0,
    PMCD_ERR_BUFFER_CHICO   = -1, /* buffer destino insuficiente             */
    PMCD_ERR_MAGIC          = -2, /* MAGIC invalido                          */
    PMCD_ERR_VERSION        = -3, /* VERSION no soportada                    */
    PMCD_ERR_PAYLOAD_LEN    = -4, /* PAYLOAD_LEN incoherente con lo recibido */
    PMCD_ERR_ARGS           = -5  /* argumentos invalidos                    */
} pmcd_res_t;

/* ------------------------------------------------------------------ */
/* Empaquetado / desempaquetado                                        */
/* ------------------------------------------------------------------ */

/*
 * pmcd_pack: serializa un mensaje (encabezado + payload) al buffer 'dst'
 * en orden de red. El campo payload_len del encabezado se ignora y se
 * recalcula a partir de payload_len_real.
 *
 *  dst           buffer destino
 *  dst_cap       capacidad de dst en bytes
 *  h             encabezado en representacion de host (magic/version/
 *                payload_len pueden venir sin fijar: se completan)
 *  payload       bytes del payload (puede ser NULL si payload_len_real==0)
 *  payload_len_real  longitud del payload
 *  out_total     [salida] total de bytes escritos (header + payload)
 *
 * Retorna PMCD_OK o un codigo PMCD_ERR_*.
 */
int pmcd_pack(uint8_t *dst, size_t dst_cap,
              const pmcd_header_t *h,
              const char *payload, size_t payload_len_real,
              size_t *out_total);

/*
 * pmcd_unpack: interpreta 'src' (al menos PMCD_HEADER_SIZE bytes) y llena
 * el encabezado 'h'. Valida MAGIC y VERSION. Si out_payload != NULL, copia
 * el payload (hasta payload_cap-1 bytes) y lo termina en '\0'.
 *
 *  src           buffer origen
 *  src_len       bytes disponibles en src (header + payload recibidos)
 *  h             [salida] encabezado en representacion de host
 *  out_payload   [salida, opcional] destino del payload como cadena
 *  payload_cap   capacidad de out_payload
 *
 * Retorna PMCD_OK o un codigo PMCD_ERR_*. PMCD_ERR_PAYLOAD_LEN indica que
 * src_len no alcanza para el payload anunciado por el encabezado.
 */
int pmcd_unpack(const uint8_t *src, size_t src_len,
                pmcd_header_t *h,
                char *out_payload, size_t payload_cap);

/* Inicializa un encabezado con MAGIC/VERSION fijos, el tipo, flags y
   seq_num dados y el timestamp actual (time(NULL)). payload_len se deja
   en 0 (pmcd_pack lo recalcula). */
void pmcd_header_init(pmcd_header_t *h, pmcd_tipo_t tipo,
                      uint8_t flags, uint32_t seq_num);

/* Nombre legible de un tipo de mensaje (para logs). */
const char *pmcd_tipo_nombre(uint8_t tipo);

/* ------------------------------------------------------------------ */
/* Payload: pares campo=valor; con escape \; y \=                      */
/* ------------------------------------------------------------------ */

/*
 * pmcd_get_field: busca 'campo' dentro de 'payload' (formato
 * campo=valor;campo=valor;...) y copia su valor ya "desescapado" en
 * out_val. Reconoce las secuencias de escape \; y \= dentro de los
 * valores.
 *
 * Retorna 1 si encontro el campo, 0 si no existe, negativo si argumentos
 * invalidos o el valor no cabe en out_cap.
 */
int pmcd_get_field(const char *payload, const char *campo,
                   char *out_val, size_t out_cap);

/*
 * pmcd_build_payload: agrega un par campo=valor al final de 'payload',
 * escapando los caracteres ; y = presentes en 'valor'. Inserta el
 * separador ';' automaticamente si el payload no esta vacio.
 *
 *  payload     buffer acumulador (cadena terminada en '\0')
 *  cap         capacidad total del buffer
 *  campo       nombre del campo (no se escapa; se asume sin ; ni =)
 *  valor       valor a escribir (se escapa)
 *
 * Retorna PMCD_OK o PMCD_ERR_* (por ejemplo, PMCD_ERR_BUFFER_CHICO).
 */
int pmcd_build_payload(char *payload, size_t cap,
                       const char *campo, const char *valor);

#endif /* PMCD_PROTOCOL_H */
