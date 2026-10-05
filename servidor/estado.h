#ifndef ESTADO_H
#define ESTADO_H

/*
 * estado.h - Estado consolidado de la infraestructura (lado servidor).
 *
 * Mantiene en memoria:
 *   - una tabla de nodos registrados (id, tipo, endpoint, estado, ultima
 *     telemetria);
 *   - un historico circular de las ultimas muestras de telemetria por nodo
 *     (al menos 5, requerimiento del enunciado).
 *
 * Todo el acceso esta protegido por un unico mutex, porque tanto los hilos
 * TCP (registro, eventos, consultas) como el hilo UDP (telemetria) leen y
 * escriben estas estructuras de forma concurrente.
 */

#include <time.h>

#define EST_MAX_NODOS       64
#define EST_HIST_POR_NODO   16   /* >= 5; potencia de 2 no requerida */
#define EST_ID_LEN          32
#define EST_TIPO_LEN        48
#define EST_ENDPOINT_LEN    64
#define EST_PAYLOAD_LEN     512  /* copia del ultimo payload de telemetria */

/* Estado operativo de un nodo (vista del servidor, Fase 1). */
typedef enum {
    NODO_DESCONECTADO = 0,
    NODO_ACTIVO,
    NODO_INACTIVO
} nodo_estado_t;

/* Una muestra de telemetria almacenada en el historico. */
typedef struct {
    time_t recibido_en;                 /* momento de recepcion en el servidor */
    char   payload[EST_PAYLOAD_LEN];    /* payload crudo campo=valor;...        */
} muestra_t;

/* Entrada de un nodo en la tabla. */
typedef struct {
    int           en_uso;
    char          id[EST_ID_LEN];
    char          tipo[EST_TIPO_LEN];
    char          endpoint[EST_ENDPOINT_LEN]; /* IP:puerto del registro       */
    nodo_estado_t estado;
    time_t        registrado_en;
    time_t        ultima_actividad;

    /* Control de duplicados/orden en UDP: ultimo SEQ_NUM aceptado. */
    unsigned int  ultimo_seq_udp;
    int           tiene_seq_udp;   /* 0 hasta recibir el primer datagrama */

    /* Historico circular de telemetria. */
    muestra_t muestras[EST_HIST_POR_NODO];
    int       hist_inicio;   /* indice de la muestra mas antigua */
    int       hist_cuenta;   /* numero de muestras validas (<= EST_HIST_POR_NODO) */
} nodo_t;

/* Inicializa la tabla y el mutex. Llamar una vez al arrancar. */
void estado_init(void);

/* Destruye el mutex. Llamar al cerrar. */
void estado_destruir(void);

/*
 * estado_registrar_nodo: registra o actualiza un nodo. Si el id ya existe,
 * refresca su tipo/endpoint y lo marca ACTIVO (re-registro). Si no existe,
 * ocupa una entrada libre.
 *
 * Retorna 0 en exito, -1 si la tabla esta llena, -2 si argumentos invalidos.
 */
int estado_registrar_nodo(const char *id, const char *tipo,
                          const char *endpoint);

/*
 * estado_nodo_registrado: 1 si el id esta registrado y en uso, 0 si no.
 */
int estado_nodo_registrado(const char *id);

/*
 * estado_agregar_muestra: agrega una muestra de telemetria al historico del
 * nodo 'id' y actualiza su ultima actividad y estado a ACTIVO.
 *
 * Retorna 0 en exito, -1 si el nodo no esta registrado.
 */
int estado_agregar_muestra(const char *id, const char *payload);

/*
 * estado_marcar_actividad: actualiza ultima_actividad y estado ACTIVO sin
 * agregar muestra (p.ej. HEARTBEAT o EVENT). Retorna 0/-1 (no registrado).
 */
int estado_marcar_actividad(const char *id);

/*
 * estado_consulta_instantanea: escribe en 'out' (cadena) el estado
 * instantaneo del nodo 'id': id, tipo, estado, endpoint y la ultima muestra
 * de telemetria si existe. Retorna 0 en exito, -1 si el nodo no existe.
 */
int estado_consulta_instantanea(const char *id, char *out, size_t out_cap);

/*
 * estado_consulta_historico: escribe en 'out' hasta 'n' muestras recientes
 * del nodo 'id' (de mas reciente a mas antigua), como texto legible.
 * Retorna el numero de muestras escritas, o -1 si el nodo no existe.
 */
int estado_consulta_historico(const char *id, int n, char *out, size_t out_cap);

/*
 * estado_resumen: escribe en 'out' un resumen de todos los nodos registrados
 * (id, estado). Util para una consulta general. Retorna el numero de nodos.
 */
int estado_resumen(char *out, size_t out_cap);

/* Nombre legible de un estado de nodo. */
const char *estado_nombre(nodo_estado_t e);

/*
 * estado_chequear_seq: control de duplicados y orden para los datagramas UDP
 * (TELEMETRY / HEARTBEAT). Compara 'seq' con el ultimo SEQ_NUM aceptado del
 * nodo 'id':
 *
 *   - si es el primer datagrama del nodo, o seq > ultimo aceptado, lo ACEPTA
 *     y actualiza el ultimo aceptado. Retorna 1.
 *   - si seq <= ultimo aceptado, lo RECHAZA (duplicado o fuera de orden) sin
 *     actualizar. Retorna 0.
 *   - si el nodo no esta registrado, retorna -1.
 *
 * Coherente con la Fase 1: no hay retransmision; solo se filtran repetidos y
 * llegadas fuera de orden usando el numero de secuencia.
 */
int estado_chequear_seq(const char *id, unsigned int seq);

/*
 * Callback que el barrido de inactividad invoca por cada cambio de estado de
 * un nodo, para que el llamador (el hilo monitor) lo registre en el log.
 *
 *  id             identificador del nodo
 *  nuevo_estado   estado al que transiciono
 *  inactivo_seg   segundos que llevaba sin actividad
 */
typedef void (*estado_cambio_cb)(const char *id, nodo_estado_t nuevo_estado,
                                 long inactivo_seg);

/*
 * estado_revisar_inactividad: recorre la tabla y aplica las transiciones de
 * la maquina de estados del nodo segun el tiempo sin actividad:
 *
 *   ACTIVO   --(sin senal > t_inactivo_seg)-->  INACTIVO
 *   INACTIVO --(sin senal > t_baja_seg)-->      baja (entrada liberada)
 *
 * Por cada transicion invoca 'cb' (si no es NULL). Devuelve el numero total
 * de nodos que cambiaron de estado en esta pasada.
 *
 * Se ejecuta bajo el mutex de estado, por lo que es seguro llamarla desde el
 * hilo monitor mientras los hilos TCP/UDP tocan la tabla.
 */
int estado_revisar_inactividad(long t_inactivo_seg, long t_baja_seg,
                               estado_cambio_cb cb);

#endif /* ESTADO_H */
