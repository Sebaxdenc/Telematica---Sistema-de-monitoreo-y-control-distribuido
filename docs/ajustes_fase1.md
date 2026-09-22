# Ajustes de la Fase 2 sobre el diseño de la Fase 1

Este documento consolida las decisiones que en la Fase 1 quedaron marcadas como
"preliminares" o "por resolver durante la implementación", y las que surgieron al
programar contra la API de sockets en la Fase 2. No reemplaza el diseño original
(`Fase1_Diseno_Arquitectura_PMCD.md`), lo complementa.

## 1. Sintaxis del payload: regla de escape fijada

La Fase 1 dejó abierta la posibilidad de "agregar algún carácter de escape para
valores que contengan `;` o `=`". En la Fase 2 se fija la regla:

- El separador entre pares es `;` y el separador campo/valor es `=`.
- Dentro de un **valor**, los caracteres `;`, `=` y `\` se escapan anteponiendo `\`.
  Es decir: `\;` → `;`, `\=` → `=`, `\\` → `\`.
- Los **nombres de campo** no se escapan (se asume que no contienen `;` ni `=`).

Ejemplo real observado en los logs:

```
nodo_id=N002;evento=UMBRAL_SUPERADO;detalle=temp\=80.9 supera umbral 80.0;severidad=ALTA
```

Implementado en `comun/pmcd_protocol.c` (`pmcd_build_payload` escapa, `pmcd_get_field`
desescapa) y verificado en `comun/test_protocolo.c`.

## 2. Resolución de nombres con `getaddrinfo` (sin IPs fijas)

Se confirma el uso de `getaddrinfo()` en **todos** los componentes (servidor, nodo y
cliente), cumpliendo el requerimiento 4 del enunciado (sin direcciones IP codificadas).
Los ejemplos base de EAFIT usaban `inet_pton` con IP fija; aquí se reemplazó por
`getaddrinfo`.

- Servidor: `getaddrinfo(NULL, puerto, ... AI_PASSIVE ...)` para TCP y UDP.
- Nodo/cliente: `getaddrinfo(host, puerto, ...)`, resolviendo el servidor por nombre.
- **Manejo de fallo de resolución sin terminar el proceso**: si `getaddrinfo` falla,
  se registra el error y la función devuelve un código de error; el nodo reintenta el
  registro (hasta 5 veces) en lugar de abortar.

Implementado en `comun/socket_utils.c`.

## 3. Autenticación: stub en Fase 2, real en Fase 3

La Fase 1 describe un servicio de autenticación separado con tokens. En la Fase 2, para
no bloquear el flujo de consultas, la autenticación es un **stub**:

- `AUTH_REQ` con un usuario no vacío recibe un `AUTH_RESP` con un token fijo
  (`TOKEN-FASE2`).
- `QUERY_REQ` exige un token **no vacío** en el payload; si falta, responde `ERROR 401`.
  No se valida el contenido del token todavía.
- El servicio de autenticación **separado** y la validación real de credenciales/roles
  se implementan en la Fase 3.

Implementado en `servidor/manejador.c` (`on_auth_req`, `on_query_req`).

## 4. Selección de transporte: confirmada

Se mantiene la decisión de la Fase 1 tras implementarla:

- **UDP** para `TELEMETRY` y `HEARTBEAT` (pérdida tolerable, alta frecuencia, sin ACK
  ni retransmisión propios en Fase 2).
- **TCP** para `REG_REQ`, `AUTH_REQ`, `EVENT`, `QUERY_REQ` y `DISCONNECT` (pérdida no
  tolerable, secuencia lógica, baja frecuencia).

El `EVENT_ACK` se mantiene como confirmación **de aplicación** sobre TCP: certifica que
el servidor procesó el evento, no solo que los bytes llegaron.

## 5. Comportamiento ante mensajes inválidos

Coherente con la Fase 1:

- **TCP**: un mensaje con `MAGIC`/`VERSION`/longitud inválidos recibe un `ERROR`
  (código 400), porque ya existe una conexión que lo justifica.
- **UDP**: un datagrama con formato inválido se registra en el log; si el error es de
  formato, se responde `ERROR` por el mismo socket UDP al remitente. La telemetría de
  un nodo **no registrado** se descarta (se deja traza en el log), sin respuesta.

Implementado en `servidor/manejador.c` (TCP) y `servidor/udp_worker.c` (UDP).

## 6. Alcance de concurrencia en Fase 2

La Fase 2 incluye una **base de concurrencia** (no la resiliencia completa de Fase 3):

- Un **hilo por conexión TCP** (detached), con un límite de `MAX_CONNECTIONS`
  conexiones activas controlado por mutex + variable de condición (patrón del ejemplo
  `c_sockets_estudio` de EAFIT).
- Un **hilo dedicado** a la recepción UDP.
- La tabla de estado se comparte entre todos los hilos y se protege con un mutex propio
  del módulo `estado`.
- **Cierre ordenado** con `SIGINT`/`SIGTERM`.

Queda para la **Fase 3**: temporizadores de aplicación, retransmisión, detección de
duplicados y control de mensajes fuera de orden (para los mensajes que lo requieran),
además de la autenticación real y las transiciones de estado ACTIVO→INACTIVO→baja de
nodos por inactividad.

## 7. Detalles de implementación menores

- El encabezado se serializa **campo por campo en big-endian**, sin depender del layout
  de structs del compilador (evita problemas de padding/alineación entre plataformas).
- `PAYLOAD_LEN` es de 2 bytes; el buffer de payload de la aplicación se limita a
  `PMCD_MAX_PAYLOAD = 4096` bytes.
- El histórico por nodo es un **buffer circular** de 16 muestras (≥5, requerimiento del
  enunciado).
- Compilación con `-std=c11` estricto: se habilitan las extensiones POSIX necesarias
  (`getaddrinfo`, `getnameinfo`, etc.) con `-D_POSIX_C_SOURCE=200112L` en el Makefile.
