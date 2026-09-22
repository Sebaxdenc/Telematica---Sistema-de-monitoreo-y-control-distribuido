/*
 * test_protocolo.c - Pruebas de round-trip del protocolo PMCD/1.0.
 *
 * Verifica:
 *  - Empaquetado/desempaquetado de cada tipo de mensaje de ejemplo de Fase 1.
 *  - Validacion de MAGIC y VERSION invalidos.
 *  - Construccion y parseo de payloads con valores que contienen ; y = (escape).
 *
 * Compilar/ejecutar:  make test
 */

#include "pmcd_protocol.h"

#include <stdio.h>
#include <string.h>

static int fallos = 0;
static int pruebas = 0;

static void check(const char *nombre, int cond) {
    pruebas++;
    if (cond) {
        printf("  [OK]   %s\n", nombre);
    } else {
        printf("  [FALLA] %s\n", nombre);
        fallos++;
    }
}

/* Round-trip: empaqueta tipo+payload, desempaqueta y compara. */
static void round_trip(pmcd_tipo_t tipo, const char *payload) {
    uint8_t buf[PMCD_MAX_MSG];
    pmcd_header_t h_in, h_out;
    size_t total = 0;
    char rec[PMCD_MAX_PAYLOAD];

    pmcd_header_init(&h_in, tipo, PMCD_FLAG_REQUIERE_ACK, 42);

    int r = pmcd_pack(buf, sizeof(buf), &h_in, payload, strlen(payload), &total);
    char nombre[128];

    snprintf(nombre, sizeof(nombre), "pack %s", pmcd_tipo_nombre(tipo));
    check(nombre, r == PMCD_OK && total == PMCD_HEADER_SIZE + strlen(payload));

    r = pmcd_unpack(buf, total, &h_out, rec, sizeof(rec));
    snprintf(nombre, sizeof(nombre), "unpack %s", pmcd_tipo_nombre(tipo));
    check(nombre, r == PMCD_OK);

    snprintf(nombre, sizeof(nombre), "campos %s (tipo/seq/flags/payload)",
             pmcd_tipo_nombre(tipo));
    check(nombre,
          h_out.tipo == (uint8_t)tipo &&
          h_out.seq_num == 42 &&
          h_out.flags == PMCD_FLAG_REQUIERE_ACK &&
          h_out.payload_len == strlen(payload) &&
          strcmp(rec, payload) == 0);
}

int main(void) {
    printf("== Pruebas de protocolo PMCD/1.0 ==\n");

    /* 1. Round-trip de los mensajes de ejemplo de la Fase 1. */
    printf("\n-- Round-trip de mensajes de ejemplo --\n");
    round_trip(PMCD_REG_REQ,
               "nodo_id=N003;tipo=sensor_temperatura;capacidades=telemetria,eventos");
    round_trip(PMCD_TELEMETRY,
               "nodo_id=N003;cpu=42.5;temp=36.1;bateria=88;estado=ACTIVO;disponibilidad=99.2");
    round_trip(PMCD_EVENT,
               "nodo_id=N003;evento=UMBRAL_SUPERADO;metrica=temp;valor=85.0;umbral=80.0;severidad=ALTA");
    round_trip(PMCD_QUERY_REQ,
               "token=a91fbe7c;nodo_id=N003;consulta=HISTORICO;metrica=temp;n=5");
    round_trip(PMCD_ERROR,
               "codigo=400;descripcion=formato_invalido;campo=payload");

    /* 2. Validacion de MAGIC y VERSION. */
    printf("\n-- Validacion de encabezado --\n");
    {
        uint8_t buf[PMCD_MAX_MSG];
        pmcd_header_t h, h2;
        size_t total = 0;
        pmcd_header_init(&h, PMCD_REG_REQ, 0, 1);
        pmcd_pack(buf, sizeof(buf), &h, "x=1", 3, &total);

        buf[0] = 0x00; /* MAGIC corrupto */
        check("MAGIC invalido -> PMCD_ERR_MAGIC",
              pmcd_unpack(buf, total, &h2, NULL, 0) == PMCD_ERR_MAGIC);

        buf[0] = PMCD_MAGIC_0;
        buf[2] = 0x09; /* VERSION no soportada */
        check("VERSION invalida -> PMCD_ERR_VERSION",
              pmcd_unpack(buf, total, &h2, NULL, 0) == PMCD_ERR_VERSION);

        buf[2] = PMCD_VERSION;
        check("truncado -> PMCD_ERR_PAYLOAD_LEN",
              pmcd_unpack(buf, PMCD_HEADER_SIZE + 1, &h2, NULL, 0)
                  == PMCD_ERR_PAYLOAD_LEN);
    }

    /* 3. Payload con escape de ; y =. */
    printf("\n-- Payload con escape (\\; y \\=) --\n");
    {
        char payload[PMCD_MAX_PAYLOAD] = "";
        char val[256];

        check("build nodo_id",
              pmcd_build_payload(payload, sizeof(payload), "nodo_id", "N007")
                  == PMCD_OK);
        /* Valor con ; y = adentro: debe escaparse y recuperarse identico. */
        check("build descripcion con ; y =",
              pmcd_build_payload(payload, sizeof(payload), "descripcion",
                                 "clave=valor;otra=cosa") == PMCD_OK);
        check("build estado",
              pmcd_build_payload(payload, sizeof(payload), "estado", "ACTIVO")
                  == PMCD_OK);

        check("get nodo_id",
              pmcd_get_field(payload, "nodo_id", val, sizeof(val)) == 1 &&
              strcmp(val, "N007") == 0);
        check("get descripcion desescapada",
              pmcd_get_field(payload, "descripcion", val, sizeof(val)) == 1 &&
              strcmp(val, "clave=valor;otra=cosa") == 0);
        check("get estado (no confundido por valor previo)",
              pmcd_get_field(payload, "estado", val, sizeof(val)) == 1 &&
              strcmp(val, "ACTIVO") == 0);
        check("get campo inexistente -> 0",
              pmcd_get_field(payload, "no_existe", val, sizeof(val)) == 0);
    }

    printf("\n== Resultado: %d/%d pruebas OK ==\n", pruebas - fallos, pruebas);
    return fallos == 0 ? 0 : 1;
}
