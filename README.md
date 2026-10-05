# Sistema de Monitoreo y Control Distribuido — PMCD/1.0

Proyecto del curso **Internet: Arquitectura y Protocolos (Telemática) — 2026-2**.
Implementa el protocolo de aplicación **PMCD/1.0** (Protocolo de Monitoreo y Control
Distribuido) sobre la API de Sockets Berkeley en C.

**Integrante:** Sebastian Andres Medina Cabezas

> **Estado: Fase 3 — Concurrencia, resiliencia y autenticación (completa).**
> Sobre la comunicación básica de la Fase 2, esta fase añade: un **servicio de
> autenticación separado** con roles y tokens, **detección de nodos inactivos** por
> temporizador, **control de duplicados/orden** en UDP y manejo robusto de todos los
> errores del enunciado. Detalle en `docs/fase3_resiliencia.md`.

## Arquitectura

Nodos y clientes se comunican **solo a través del servidor central**; nunca entre sí.
El servidor, a su vez, consulta al **servicio de autenticación** (proceso aparte).

```
   Nodo 1 ─┐  UDP: TELEMETRY, HEARTBEAT
           ├──────────────────────────────►┌──────────────────┐   TCP: AUTH_REQ
   Nodo 2 ─┘  TCP: REG_REQ, EVENT          │  Servidor central │◄────────────────┐
                                           │   (C, Berkeley)   │                 │
   Cliente ────────────────────────────────►│  tabla+histórico │        ┌────────▼────────┐
              TCP: AUTH_REQ, QUERY_REQ      │  + sesiones      │        │ Servicio de auth │
                                           └──────────────────┘        │ (proceso aparte) │
                                                                        └──────────────────┘
```

- **Servidor central** (`servidor/`): C puro con Sockets Berkeley. Registra nodos,
  recibe telemetría (UDP) y eventos (TCP), mantiene estado, histórico y sesiones en
  memoria y responde consultas. Atiende conexiones TCP concurrentes (un hilo por
  conexión, con límite), procesa UDP en un hilo dedicado y detecta nodos inactivos en
  un hilo monitor. Recibe por parámetro **puerto**, **archivo de logs** y la ubicación
  del **servicio de auth**. Registra cada petición/respuesta con **IP y puerto de
  origen**.
- **Servicio de autenticación** (`auth/`): proceso independiente con su propio almacén
  de credenciales y roles. Valida usuario/clave y emite un token de sesión.
- **Nodo** (`nodo/`): simula un sensor. Se registra, envía telemetría periódica por
  UDP y reporta eventos críticos por TCP. Se re-registra si el servidor deja de
  reconocerlo.
- **Cliente de administración** (`cliente/`): se autentica (usuario/clave), consulta el
  estado instantáneo, el histórico (≥5 muestras) y un resumen. Se re-autentica solo si
  su token expira.
- **Código compartido** (`comun/`): protocolo PMCD, utilidades de sockets (con
  `getaddrinfo`, sin IPs fijas) y logging.

```
.
├── Makefile                 # compila auth, servidor, nodo y cliente en bin/
├── README.md
├── docs/
│   ├── Fase1_Diseno_Arquitectura_PMCD.md   # diseño de Fase 1
│   ├── ajustes_fase1.md                    # decisiones consolidadas
│   └── fase3_resiliencia.md                # concurrencia, auth, resiliencia, errores
├── scripts/
│   └── demo_fase3.sh        # demo integral reproducible
├── comun/
│   ├── pmcd_protocol.h/.c    # tipos, encabezado 16B, pack/unpack, payload
│   ├── socket_utils.h/.c     # getaddrinfo, sockets TCP/UDP, send_all, recv_msg
│   └── log_utils.h/.c        # logging a consola + archivo con IP:puerto
├── auth/
│   └── auth.c                # servicio de autenticación separado
├── servidor/
│   ├── servidor.c            # main: args, hilos UDP/monitor, aceptación TCP concurrente
│   ├── estado.h/.c           # tabla de nodos + histórico + SEQ_NUM (mutex)
│   ├── manejador.h/.c        # interpretación de mensajes TCP + auth real
│   ├── udp_worker.h/.c       # hilo de recepción UDP + duplicados/orden
│   ├── monitor.h/.c          # hilo de detección de inactividad
│   ├── sesiones.h/.c         # tabla de sesiones/tokens
│   └── auth_cliente.h/.c     # cliente del servicio de auth
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
make              # compila los cuatro ejecutables en bin/
make auth         # solo el servicio de autenticación
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

Se levantan **cuatro procesos** (una terminal por componente, todas en WSL, en la raíz
del repo). El orden recomendado es: auth → servidor → nodos → cliente.

**1. Servicio de autenticación** — `puerto`:

```bash
./bin/auth 6000
```

**2. Servidor central** — `puerto archivoDeLogs authHost authPuerto`:

```bash
./bin/servidor 5000 servidor.log localhost 6000
```

**3. Nodo(s)** — `host puerto nodo_id [tipo] [intervalo_seg]`:

```bash
./bin/nodo localhost 5000 N001 sensor_temp 2
./bin/nodo localhost 5000 N002 sensor_humedad 3
```

**4. Cliente de administración** — `host puerto usuario clave [comando...]`:

```bash
# Modo interactivo
./bin/cliente localhost 5000 admin admin123

# Comando único
./bin/cliente localhost 5000 admin admin123 estado N001
./bin/cliente localhost 5000 admin admin123 historico N001 5
./bin/cliente localhost 5000 operador oper123 resumen
```

Credenciales de ejemplo (definidas en el servicio de auth):

| Usuario    | Clave      | Rol           |
|------------|------------|---------------|
| `admin`    | `admin123` | administrador |
| `operador` | `oper123`  | operador      |

Comandos del cliente:

| Comando | Descripción |
|---|---|
| `estado <nodo_id>` | estado instantáneo (última telemetría) |
| `historico <nodo_id> [n]` | últimas `n` muestras (por defecto 5) |
| `resumen` | lista de nodos y su estado |
| `salir` | termina el cliente |

Para detener el servidor, `Ctrl+C`: cierra los sockets, detiene los hilos UDP y
monitor y cierra el log de forma ordenada.

### Demo automática

```bash
bash scripts/demo_fase3.sh   # levanta todo y ejercita auth, telemetría, consultas y errores
```

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
(token ausente/ inválido/expirado o credenciales inválidas), `409` nodo no registrado,
`422` tipo no soportado, `503` servicio de auth no disponible.

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
| Reglas: nodo no registrado, token, formato | `servidor/manejador.c` (códigos 400/401/409/422/503) |
| Concurrencia (hilo por conexión + hilo UDP + monitor) | `servidor/servidor.c` (mutex/cond, `MAX_CONNECTIONS`) |
| Logging con IP:puerto a consola y archivo | `comun/log_utils.c`: `log_evento` |
| Autenticación separada (roles, tokens) | `auth/auth.c`; `servidor/auth_cliente.c`; `servidor/sesiones.c` |
| Detección de nodos inactivos (temporizador) | `servidor/monitor.c`; `servidor/estado.c`: `estado_revisar_inactividad` |
| Duplicados / orden en UDP (SEQ_NUM) | `servidor/estado.c`: `estado_chequear_seq`; `servidor/udp_worker.c` |
| Re-registro del nodo / re-autenticación del cliente | `nodo/nodo.c`: `enviar_evento`; `cliente/cliente.c`: `consultar` |

## Pruebas rápidas

```bash
make test         # 25/25 pruebas del protocolo
make test-socket  # 8/8 pruebas de sockets (resolución + intercambio PMCD)
bash scripts/demo_fase3.sh   # demo integral de Fase 3
```

## Fases

- **Fase 1** — Diseño y arquitectura (`docs/Fase1_Diseno_Arquitectura_PMCD.md`).
- **Fase 2** — Comunicación básica.
- **Fase 3** — Concurrencia, resiliencia y autenticación *(esta entrega)*. Servicio de
  auth separado, detección de inactividad, control de duplicados/orden y manejo de
  errores. Ver `docs/fase3_resiliencia.md`.
