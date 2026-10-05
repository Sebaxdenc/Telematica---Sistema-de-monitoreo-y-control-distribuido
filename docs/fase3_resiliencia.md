# Fase 3 — Concurrencia, resiliencia y autenticación

Este documento describe lo que agrega la Fase 3 sobre la comunicación básica de la
Fase 2, y define el comportamiento del sistema ante cada situación (para la
especificación del protocolo y la sustentación).

## 1. Estrategia de concurrencia (justificación)

El servidor central atiende múltiples clientes y nodos de forma concurrente con esta
estrategia:

- **Un hilo por conexión TCP** (`pthread` detached), con un límite de
  `MAX_CONNECTIONS` conexiones activas controlado por un mutex y una variable de
  condición. Se eligió *hilo por conexión* porque las operaciones TCP (registro, auth,
  eventos, consultas) son transaccionales y de baja frecuencia; un hilo por conexión es
  simple de razonar y suficiente para la carga esperada. El límite evita agotar
  recursos si llegan muchas conexiones a la vez.
- **Un hilo dedicado a la recepción UDP** (`recvfrom` en bucle), separado de los hilos
  TCP, porque la telemetría es de alta frecuencia y conviene procesarla sin competir
  por el ciclo de aceptación TCP.
- **Un hilo monitor** que revisa periódicamente la inactividad de los nodos.
- La **tabla de estado** compartida se protege con un mutex propio del módulo `estado`;
  la **tabla de sesiones** con el suyo; el **log** con el suyo. Cada recurso compartido
  tiene un único mutex y el orden de adquisición nunca forma ciclos, por lo que no hay
  riesgo de interbloqueo.

## 2. Servicio de autenticación separado

El requerimiento 6 pide evitar un sistema de usuarios basado únicamente en la
aplicación principal. Por eso la autenticación vive en un **proceso independiente**
(`auth/auth.c`, binario `bin/auth`) con su propio almacén de credenciales y roles.

### 2.1 Flujo

```
Cliente --AUTH_REQ(usuario,clave)--> Servidor central
                                       |
                                       |  (el servidor es CLIENTE del servicio auth)
                                       v
                             Servicio de auth  --valida credenciales-->
                                       |
                       AUTH_RESP(resultado, rol, token)
                                       |
Servidor central <---------------------+
   crea una SESION local (token -> usuario, rol, expiracion)
   |
   +--AUTH_RESP(OK, rol, token)--> Cliente

Cliente --QUERY_REQ(token, ...)--> Servidor central
   valida el token contra su tabla de sesiones (sin volver a pedir la clave)
   +--QUERY_RESP(...) o ERROR 401 si el token no es valido
```

### 2.2 Roles y credenciales de ejemplo

| Usuario    | Clave      | Rol            |
|------------|------------|----------------|
| `admin`    | `admin123` | administrador  |
| `operador` | `oper123`  | operador       |

El rol viaja en `AUTH_RESP` y queda asociado a la sesión. En esta entrega ambos roles
pueden consultar; el rol queda registrado para poder aplicar permisos diferenciados en
el futuro sin cambiar el protocolo.

### 2.3 Tokens y sesiones

- El servicio de auth emite un **token opaco** (16 caracteres hexadecimales aleatorios).
- El servidor central guarda la sesión en memoria con una **expiración** de
  `SES_TTL_SEG` (300 s por defecto). Cada `QUERY_REQ` valida el token contra esa tabla.
- Si el token no existe o expiró, el servidor responde `ERROR 401`.

## 3. Detección de nodos inactivos (temporizadores)

Un hilo monitor revisa la tabla de nodos cada `MON_PERIODO` segundos y aplica la
máquina de estados del nodo:

```
            sin señal > MON_T_INACTIVO           sin señal > MON_T_BAJA
  ACTIVO ---------------------------> INACTIVO ------------------------> (baja)
    ^                                                                     |
    +----------------- llega telemetría/heartbeat/evento -----------------+
```

- `MON_T_INACTIVO` = 10 s: sin telemetría ni heartbeat, el nodo pasa a **INACTIVO**.
- `MON_T_BAJA` = 30 s: si sigue sin señal, se le da de **baja** (se libera su entrada y
  se exige un nuevo registro).
- Cada transición se registra en el log (`tipo=MONITOR`).

Esto resuelve el caso de un nodo apagado: deja de aparecer como activo y, tras el
segundo umbral, desaparece de la tabla.

Del lado del **nodo**, si tras ser dado de baja intenta enviar un `EVENT`, el servidor
responde `ERROR 409` (nodo no registrado); el nodo detecta ese código y **se
re-registra** automáticamente antes de continuar.

## 4. Control de duplicados y orden en UDP

Coherente con la decisión de la Fase 1 de **no** añadir retransmisión a la telemetría
(una muestra perdida se reemplaza por la siguiente), la Fase 3 sí agrega un filtro
barato usando el `SEQ_NUM` del encabezado:

- El servidor guarda, por nodo, el **último `SEQ_NUM` aceptado** por UDP.
- Un datagrama con `SEQ_NUM` **mayor** al último se acepta y actualiza la referencia.
- Un datagrama con `SEQ_NUM` **menor o igual** se descarta como **duplicado o fuera de
  orden**, con traza en el log. No se procesa ni contamina el histórico.

No hay ACK ni retransmisión para telemetría/heartbeat: solo se filtran repetidos y
llegadas tardías.

## 5. Tabla situación de error → comportamiento

| Situación | Detección | Comportamiento |
|---|---|---|
| Mensaje mal formado (MAGIC/VERSION/longitud) por **TCP** | `pmcd_unpack` / `su_recv_pmcd_msg` | `ERROR 400` y se cierra la conexión; queda en el log |
| Datagrama mal formado por **UDP** | `pmcd_unpack` en el hilo UDP | Se registra; se responde `ERROR 400` al remitente por el mismo socket |
| Parámetros inválidos (args) | Validación en cada `main` | Mensaje de uso y salida con código ≠ 0 |
| Falta un campo requerido (p. ej. `nodo_id`) | Handlers del manejador | `ERROR 400` con descripción |
| Nodo no registrado envía EVENT/telemetría | `estado_nodo_registrado` | TCP: `ERROR 409`. UDP: se descarta con traza |
| Credenciales inválidas | Servicio de auth | `AUTH_RESP FALLO` → el servidor responde `ERROR 401` |
| Servicio de auth caído/inalcanzable | `auth_cliente_validar` → `AUTHC_SIN_SERVICIO` | `ERROR 503`; el servidor **sigue vivo** |
| Token ausente en consulta | `on_query_req` | `ERROR 401` (token ausente) |
| Token inválido | `sesiones_validar` → 0 | `ERROR 401` (token inválido) |
| Token expirado | `sesiones_validar` → -1 | `ERROR 401` (token expirado); el cliente se re-autentica |
| Tipo de mensaje no soportado | `switch` del manejador / hilo UDP | `ERROR 422` |
| Desconexión abrupta del cliente/nodo | `recv` devuelve 0/-1 | Se cierra esa conexión y se limpia; el servidor sigue |
| Nodo inactivo (temporizador) | Hilo monitor | ACTIVO→INACTIVO→baja, con traza `MONITOR` |
| Duplicado / fuera de orden (UDP) | `estado_chequear_seq` | Se descarta con traza; no altera el estado |

Códigos de error de aplicación (payload de `ERROR`): **400** formato/campo faltante,
**401** no autorizado (token ausente/ inválido/expirado o credenciales inválidas),
**409** nodo no registrado, **422** tipo no soportado, **503** servicio de auth no
disponible.

## 6. Cómo ejecutar y demostrar

Se necesitan cuatro procesos (WSL/Linux). En terminales separadas o con el script:

```bash
make                                          # compila auth, servidor, nodo, cliente
./bin/auth 6000                               # 1. servicio de autenticacion
./bin/servidor 5300 servidor.log localhost 6000  # 2. servidor central (apunta al auth)
./bin/nodo localhost 5300 N001 sensor_temp 1  # 3. uno o varios nodos
./bin/cliente localhost 5300 admin admin123 resumen   # 4. cliente

# Demo automatica de todo lo anterior:
bash scripts/demo_fase3.sh
```

## 7. Limitaciones y notas

- **Credenciales en el log**: por simplicidad de depuración, el `AUTH_REQ` se registra
  con la clave en texto plano. En un sistema real no se registraría la clave; aquí se
  mantiene por su valor didáctico y porque el alcance es académico.
- **Almacén de credenciales en memoria**: el servicio de auth usa una tabla fija en
  código. Está *separado* de la aplicación principal (que es lo que pide el enunciado),
  pero no es persistente.
- **Estado en memoria**: el servidor pierde la tabla de nodos y sesiones al reiniciar;
  los nodos se re-registran automáticamente al detectar `ERROR 409`.
