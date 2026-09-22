/*
 * pmcd_protocol.c - Implementacion del protocolo PMCD/1.0.
 *
 * Serializacion del encabezado de 16 bytes en orden de red (big-endian)
 * sin depender del layout de structs del compilador: se escribe/lee campo
 * por campo. El payload es texto (pares campo=valor;) con escape \; y \=.
 */

#include "pmcd_protocol.h"

#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Helpers de codificacion big-endian                                  */
/* ------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ------------------------------------------------------------------ */
/* Encabezado                                                          */
/* ------------------------------------------------------------------ */

void pmcd_header_init(pmcd_header_t *h, pmcd_tipo_t tipo,
                      uint8_t flags, uint32_t seq_num) {
    if (h == NULL) {
        return;
    }
    h->magic[0]    = PMCD_MAGIC_0;
    h->magic[1]    = PMCD_MAGIC_1;
    h->version     = PMCD_VERSION;
    h->tipo        = (uint8_t)tipo;
    h->flags       = flags;
    h->seq_num     = seq_num;
    h->timestamp   = (uint32_t)time(NULL);
    h->payload_len = 0;
    h->reservado   = 0;
}

const char *pmcd_tipo_nombre(uint8_t tipo) {
    switch (tipo) {
        case PMCD_REG_REQ:    return "REG_REQ";
        case PMCD_REG_RESP:   return "REG_RESP";
        case PMCD_AUTH_REQ:   return "AUTH_REQ";
        case PMCD_AUTH_RESP:  return "AUTH_RESP";
        case PMCD_TELEMETRY:  return "TELEMETRY";
        case PMCD_EVENT:      return "EVENT";
        case PMCD_EVENT_ACK:  return "EVENT_ACK";
        case PMCD_QUERY_REQ:  return "QUERY_REQ";
        case PMCD_QUERY_RESP: return "QUERY_RESP";
        case PMCD_HEARTBEAT:  return "HEARTBEAT";
        case PMCD_ERROR:      return "ERROR";
        case PMCD_DISCONNECT: return "DISCONNECT";
        default:              return "DESCONOCIDO";
    }
}

int pmcd_pack(uint8_t *dst, size_t dst_cap,
              const pmcd_header_t *h,
              const char *payload, size_t payload_len_real,
              size_t *out_total) {
    if (dst == NULL || h == NULL) {
        return PMCD_ERR_ARGS;
    }
    if (payload_len_real > PMCD_MAX_PAYLOAD) {
        return PMCD_ERR_PAYLOAD_LEN;
    }
    if (payload_len_real > 0 && payload == NULL) {
        return PMCD_ERR_ARGS;
    }

    size_t total = PMCD_HEADER_SIZE + payload_len_real;
    if (dst_cap < total) {
        return PMCD_ERR_BUFFER_CHICO;
    }

    /* Encabezado (offsets fijos, big-endian). */
    dst[0]  = PMCD_MAGIC_0;
    dst[1]  = PMCD_MAGIC_1;
    dst[2]  = PMCD_VERSION;
    dst[3]  = h->tipo;
    dst[4]  = h->flags;
    put_u32(&dst[5], h->seq_num);
    put_u32(&dst[9], h->timestamp);
    put_u16(&dst[13], (uint16_t)payload_len_real);
    dst[15] = h->reservado;

    if (payload_len_real > 0) {
        memcpy(&dst[PMCD_HEADER_SIZE], payload, payload_len_real);
    }

    if (out_total != NULL) {
        *out_total = total;
    }
    return PMCD_OK;
}

int pmcd_unpack(const uint8_t *src, size_t src_len,
                pmcd_header_t *h,
                char *out_payload, size_t payload_cap) {
    if (src == NULL || h == NULL) {
        return PMCD_ERR_ARGS;
    }
    if (src_len < PMCD_HEADER_SIZE) {
        return PMCD_ERR_PAYLOAD_LEN;
    }

    h->magic[0]    = src[0];
    h->magic[1]    = src[1];
    h->version     = src[2];
    h->tipo        = src[3];
    h->flags       = src[4];
    h->seq_num     = get_u32(&src[5]);
    h->timestamp   = get_u32(&src[9]);
    h->payload_len = get_u16(&src[13]);
    h->reservado   = src[15];

    if (h->magic[0] != PMCD_MAGIC_0 || h->magic[1] != PMCD_MAGIC_1) {
        return PMCD_ERR_MAGIC;
    }
    if (h->version != PMCD_VERSION) {
        return PMCD_ERR_VERSION;
    }
    if (h->payload_len > PMCD_MAX_PAYLOAD) {
        return PMCD_ERR_PAYLOAD_LEN;
    }
    /* El buffer debe contener el payload completo anunciado. */
    if (src_len < (size_t)PMCD_HEADER_SIZE + h->payload_len) {
        return PMCD_ERR_PAYLOAD_LEN;
    }

    if (out_payload != NULL) {
        if (payload_cap == 0) {
            return PMCD_ERR_BUFFER_CHICO;
        }
        size_t n = h->payload_len;
        if (n > payload_cap - 1) {
            n = payload_cap - 1; /* truncamos para no desbordar */
        }
        memcpy(out_payload, &src[PMCD_HEADER_SIZE], n);
        out_payload[n] = '\0';
    }

    return PMCD_OK;
}

/* ------------------------------------------------------------------ */
/* Payload: campo=valor; con escape \; y \=                            */
/* ------------------------------------------------------------------ */

int pmcd_build_payload(char *payload, size_t cap,
                       const char *campo, const char *valor) {
    if (payload == NULL || campo == NULL || valor == NULL || cap == 0) {
        return PMCD_ERR_ARGS;
    }

    size_t len = strlen(payload);
    /* Trabajamos con indices para controlar el limite en cada escritura. */
#define APPEND_CHAR(c)                     \
    do {                                   \
        if (len + 1 >= cap) {              \
            return PMCD_ERR_BUFFER_CHICO;  \
        }                                  \
        payload[len++] = (c);              \
    } while (0)

    if (len > 0) {
        APPEND_CHAR(';');
    }

    for (const char *p = campo; *p != '\0'; ++p) {
        APPEND_CHAR(*p);
    }
    APPEND_CHAR('=');

    for (const char *p = valor; *p != '\0'; ++p) {
        if (*p == ';' || *p == '=' || *p == '\\') {
            APPEND_CHAR('\\'); /* escape */
        }
        APPEND_CHAR(*p);
    }

    payload[len] = '\0';
#undef APPEND_CHAR
    return PMCD_OK;
}

int pmcd_get_field(const char *payload, const char *campo,
                   char *out_val, size_t out_cap) {
    if (payload == NULL || campo == NULL || out_val == NULL || out_cap == 0) {
        return PMCD_ERR_ARGS;
    }

    size_t campo_len = strlen(campo);
    const char *p = payload;

    while (*p != '\0') {
        /* Comparar el nombre del campo hasta el '=' (los nombres no llevan
           escape). */
        int coincide = (strncmp(p, campo, campo_len) == 0 &&
                        p[campo_len] == '=');

        if (coincide) {
            const char *v = p + campo_len + 1; /* inicio del valor */
            size_t o = 0;
            while (*v != '\0') {
                char c = *v;
                if (c == '\\') {
                    /* Secuencia de escape: el siguiente char es literal. */
                    v++;
                    if (*v == '\0') {
                        break;
                    }
                    c = *v;
                } else if (c == ';') {
                    break; /* fin del valor (separador sin escape) */
                }
                if (o + 1 >= out_cap) {
                    return PMCD_ERR_BUFFER_CHICO;
                }
                out_val[o++] = c;
                v++;
            }
            out_val[o] = '\0';
            return 1;
        }

        /* Avanzar hasta el siguiente separador ';' sin escape para no
           confundir un campo con el sufijo de un valor. */
        while (*p != '\0') {
            if (*p == '\\' && *(p + 1) != '\0') {
                p += 2;
                continue;
            }
            if (*p == ';') {
                p++;
                break;
            }
            p++;
        }
    }

    return 0; /* no encontrado */
}
