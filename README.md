# Sistema de Monitoreo y Control Distribuido — PMCD/1.0

Proyecto del curso **Internet: Arquitectura y Protocolos (Telemática) — 2026-2**.
Implementa el protocolo de aplicación **PMCD/1.0** (Protocolo de Monitoreo y Control
Distribuido) sobre la API de Sockets Berkeley en C.

**Integrante:** Sebastian Andres Medina Cabezas

> **Estado: Fase 2 — Comunicación básica.** El diseño del protocolo se definió en la
> Fase 1 (`docs/`). Esta fase implementa la comunicación funcional entre nodos,
> servidor central y cliente de administración: creación/configuración de sockets,
> establecimiento de la comunicación, envío/recepción/interpretación de mensajes,
> construcción de respuestas y las reglas básicas del protocolo. Incluye una base de
> concurrencia (hilo por conexión TCP + hilo dedicado UDP) que prepara la Fase 3.

## Arquitectura

Nodos y clientes se comunican **solo a través del servidor central**; nunca entre sí.

```
   Nodo 1 ─┐  UDP: TELEMETRY, HEARTBEAT
           ├──────────────────────────────►┌──────────────────┐
   Nodo 2 ─┘  TCP: REG_REQ, EVENT          │  Servidor central │
                                           │   (C, Berkeley)   │
   Cliente ────────────────────────────────►│  tabla + histórico│
              TCP: AUTH_REQ, QUERY_REQ      └──────────────────┘
```

- **Servidor central** (`servidor/`): C puro con Sockets Berkeley. Registra nodos,
  recibe telemetría (UDP) y eventos (TCP), mantiene el estado y el histórico en
  memoria y responde consultas. Atiende conexiones TCP concurrentes (un hilo por
  conexión) y procesa la telemetría UDP en un hilo dedicado. Recibe por parámetro el
  **puerto** y el **archivo de logs**, y registra cada petición/respuesta en consola y
  archivo con la **IP y puerto de origen**.
- **Nodo** (`nodo/`): simula un sensor. Se registra, envía telemetría periódica por
  UDP y reporta eventos críticos por TCP.
- **Cliente de administración** (`cliente/`): se autentica y consulta el estado
  instantáneo, el histórico (≥5 muestras) y un resumen de los nodos.
- **Código compartido** (`comun/`): protocolo PMCD, utilidades de sockets (con
  `getaddrinfo`, sin IPs fijas) y logging.

```
.
├── Makefile                 # compila servidor, nodo y cliente en bin/
├── README.md
├── docs/
│   ├── Fase1_Diseno_Arquitectura_PMCD.md   # diseño de Fase 1 (copia en el repo)
│   └── ajustes_fase1.md                    # decisiones consolidadas en Fase 2
├── comun/
│   ├── pmcd_protocol.h/.c    # tipos, encabezado 16B, pack/unpack, payload
│   ├── socket_utils.h/.c     # getaddrinfo, sockets TCP/UDP, send_all, recv_msg
│   ├── log_utils.h/.c        # logging a consola + archivo con IP:puerto
│   ├── test_protocolo.c      # pruebas del protocolo (make test)
│   └── test_socket.c         # pruebas de sockets (make test-socket)
├── servidor/
│   ├── servidor.c            # main: args, hilo UDP, aceptación TCP concurrente
│   ├── estado.h/.c           # tabla de nodos + histórico (mutex)
│   ├── manejador.h/.c        # interpretación de mensajes TCP
│   └── udp_worker.h/.c       # hilo de recepción UDP
├── nodo/nodo.c
└── cliente/cliente.c
```

## Requisitos

- **WSL/Linux** (headers POSIX: `sys/socket.h`, `netdb.h`, `pthread`).
- `gcc` con soporte C11, `make`.

> El repositorio vive en una ruta de Windows/OneDrive; compílalo desde WSL accediendo
> vía `/mnt/c/...`.

## Compilación

```bash
make              # compila los tres ejecutables en bin/
make servidor     # solo el servidor
make nodo         # solo el nodo
make cliente      # solo el cliente
make test         # pruebas del protocolo (round-trip, escape, validación)
make test-socket  # pruebas de las utilidades de socket
make clean        # elimina bin/ y los *.log
```

Flags de compilación: `-std=c11 -Wall -Wextra -Wpedantic -pthread`. El proyecto
compila sin warnings.

## Ejecución

Abre una terminal por componente (todas en WSL, en la raíz del repo).

**1. Servidor** — recibe `puerto` y `archivoDeLogs`:

```bash
./bin/servidor 5000 servidor.log
```

**2. Nodo(s)** — `host puerto nodo_id [tipo] [intervalo_seg]`:

```bash
./bin/nodo localhost 5000 N001 sensor_temp 2
./bin/nodo localhost 5000 N002 sensor_humedad 3
```

**3. Cliente de administración** — `host puerto usuario [comando...]`:

```bash
# Modo interactivo
./bin/cliente localhost 5000 admin

# Comando único
./bin/cliente localhost 5000 admin estado N001
./bin/cliente localhost 5000 admin historico N001 5
./bin/cliente localhost 5000 admin resumen
```

Comandos del cliente:

| Comando | Descripción |
|---|---|
| `estado <nodo_id>` | estado instantáneo (última telemetría) |
| `historico <nodo_id> [n]` | últimas `n` muestras (por defecto 5) |
| `resumen` | lista de nodos y su estado |
| `salir` | termina el cliente |

Para detener el servidor, `Ctrl+C`: cierra los sockets, detiene el hilo UDP y cierra
el log de forma ordenada.

## Protocolo PMCD/1.0

Modelo cliente-servidor sobre TCP/IP. Encabezado binario de tamaño fijo (16 bytes, en
orden de red) seguido de un payload de texto `campo=valor;`. Detalle completo en
`docs/Fase1_Diseno_Arquitectura_PMCD.md` y los ajustes de Fase 2 en
`docs/ajustes_fase1.md`.

**Encabezado (16 bytes):** `MAGIC(2)="PM"`, `VERSION(1)=1`, `TYPE(1)`, `FLAGS(1)`,
`SEQ_NUM(4)`, `TIMESTAMP(4)`, `PAYLOAD_LEN(2)`, `RESERVED(1)`.

**Flags:** bit0 `REQUIERE_ACK`, bit1 `ES_RESPUESTA`, bit2 `REENVIO`.

**Payload:** pares `campo=valor` separados por `;`. Los caracteres `;`, `=` y `\`
dentro de un valor se escapan con `\` (por ejemplo `detalle=temp\=80.9`).

### Mensajes implementados

| Código | Mensaje | Transporte | Dirección |
|---|---|---|---|
| 0x01 | `REG_REQ` | TCP | Nodo → Servidor |
| 0x02 | `REG_RESP` | TCP | Servidor → Nodo |
| 0x03 | `AUTH_REQ` | TCP | Cliente → Servidor |
| 0x04 | `AUTH_RESP` | TCP | Servidor → Cliente |
| 0x05 | `TELEMETRY` | UDP | Nodo → Servidor |
| 0x06 | `EVENT` | TCP | Nodo → Servidor |
| 0x07 | `EVENT_ACK` | TCP | Servidor → Nodo |
| 0x08 | `QUERY_REQ` | TCP | Cliente → Servidor |
| 0x09 | `QUERY_RESP` | TCP | Servidor → Cliente |
| 0x0A | `HEARTBEAT` | UDP | Nodo → Servidor |
| 0x0B | `ERROR` | TCP/UDP | Servidor → Nodo/Cliente |
| 0x0C | `DISCONNECT` | TCP | Nodo/Cliente → Servidor |

Códigos de error (payload de `ERROR`): `400` formato inválido, `401` no autorizado
(token ausente/ inválido), `409` nodo no registrado, `422` tipo no soportado.

## Mapeo especificación ↔ código

Este mapa conecta la especificación del protocolo (Fase 1) con la implementación, útil
para la sustentación:

| Concepto de la especificación | Dónde está en el código |
|---|---|
| Encabezado de 16 bytes, orden de red | `comun/pmcd_protocol.c`: `pmcd_pack` / `pmcd_unpack` (`put_u16/u32`, `get_u16/u32`) |
| Validación de `MAGIC` / `VERSION` | `comun/pmcd_protocol.c`: `pmcd_unpack` |
| Payload `campo=valor;` con escape | `comun/pmcd_protocol.c`: `pmcd_build_payload` / `pmcd_get_field` |
| Resolución por nombre (sin IP fija) | `comun/socket_utils.c`: `getaddrinfo` en cada función de socket |
| Lectura de mensaje completo por TCP | `comun/socket_utils.c`: `su_recv_pmcd_msg` |
| Registro de un nodo (`REG_REQ`/`REG_RESP`) | `servidor/manejador.c`: `on_reg_req`; `nodo/nodo.c`: `registrar` |
| Telemetría por UDP (`TELEMETRY`) | `servidor/udp_worker.c`; `nodo/nodo.c`: `enviar_udp` |
| Evento con confirmación (`EVENT`/`EVENT_ACK`) | `servidor/manejador.c`: `on_event`; `nodo/nodo.c`: `enviar_evento` |
| Consulta y respuesta (`QUERY_REQ`/`QUERY_RESP`) | `servidor/manejador.c`: `on_query_req`; `cliente/cliente.c` |
| Estado consolidado + histórico | `servidor/estado.c` |
| Reglas: nodo no registrado, token, formato | `servidor/manejador.c` (códigos 400/401/409/422) |
| Concurrencia (hilo por conexión + hilo UDP) | `servidor/servidor.c` (mutex/cond, `MAX_CONNECTIONS`) |
| Logging con IP:puerto a consola y archivo | `comun/log_utils.c`: `log_evento` |

## Pruebas rápidas

```bash
make test         # 25/25 pruebas del protocolo
make test-socket  # 8/8 pruebas de sockets (resolución + intercambio PMCD)
```

## Fases

- **Fase 1** — Diseño y arquitectura (`docs/Fase1_Diseno_Arquitectura_PMCD.md`).
- **Fase 2** — Comunicación básica *(esta entrega)*.
- **Fase 3** — Concurrencia avanzada, resiliencia (timeouts, retransmisión,
  duplicados) y autenticación real. La base de concurrencia ya está sentada aquí.
