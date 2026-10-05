#!/usr/bin/env bash
#
# demo_fase3.sh - Demostracion integral del sistema PMCD/1.0 (Fase 3).
#
# Levanta los cuatro componentes (servicio de autenticacion, servidor central,
# nodos y cliente) y ejercita los mecanismos de la Fase 3:
#   - autenticacion real contra el servicio separado (roles administrador/operador)
#   - concurrencia (varios nodos + consultas simultaneas)
#   - telemetria (UDP), eventos (TCP con ACK) y consultas (estado/historico/resumen)
#   - control de errores (credenciales invalidas, token invalido, nodo desconocido)
#
# Uso:
#   make            # compilar primero
#   bash scripts/demo_fase3.sh
#
# Requiere WSL/Linux, python3 (solo para pausas) opcional. Ajusta los puertos si
# estan ocupados. Los procesos se limpian al terminar.

set -u
cd "$(dirname "$0")/.."   # raiz del repo

AUTH_PORT=6000
SRV_PORT=5300
LOG=demo_servidor.log

# --- limpieza previa ---
rm -f "$LOG"
pids=()
cleanup() {
    echo
    echo "=== deteniendo procesos ==="
    for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null; done
    # dar tiempo al cierre ordenado del servidor
    sleep 1
    for pid in "${pids[@]:-}"; do kill -9 "$pid" 2>/dev/null; done
}
trap cleanup EXIT

echo "==================================================================="
echo " DEMO FASE 3 - Sistema de Monitoreo y Control Distribuido (PMCD/1.0)"
echo "==================================================================="

# --- verificar binarios ---
for b in auth servidor nodo cliente; do
    if [ ! -x "bin/$b" ]; then
        echo "Falta bin/$b. Ejecuta 'make' primero."; exit 1
    fi
done

# 1) Servicio de autenticacion (proceso separado)
echo
echo "### 1. Servicio de autenticacion en el puerto $AUTH_PORT ###"
./bin/auth "$AUTH_PORT" > /tmp/demo_auth.txt 2>&1 &
pids+=($!)
sleep 0.5

# 2) Servidor central (apunta al servicio de auth)
echo "### 2. Servidor central en el puerto $SRV_PORT (auth en localhost:$AUTH_PORT) ###"
./bin/servidor "$SRV_PORT" "$LOG" localhost "$AUTH_PORT" > /tmp/demo_srv.txt 2>&1 &
pids+=($!)
sleep 0.5

# 3) Dos nodos concurrentes
echo "### 3. Dos nodos reportando telemetria (N001, N002) ###"
./bin/nodo localhost "$SRV_PORT" N001 sensor_temp 1 > /tmp/demo_n1.txt 2>&1 &
pids+=($!)
./bin/nodo localhost "$SRV_PORT" N002 sensor_humedad 2 > /tmp/demo_n2.txt 2>&1 &
pids+=($!)
sleep 5

# 4) Consultas del cliente con auth real
echo
echo "### 4. Cliente ADMINISTRADOR (admin) ###"
echo "--- resumen ---"
./bin/cliente localhost "$SRV_PORT" admin admin123 resumen | grep -E "Autenticado|RESUMEN"
echo "--- estado instantaneo de N001 ---"
./bin/cliente localhost "$SRV_PORT" admin admin123 estado N001 | grep -E "INSTANTANEO"
echo "--- historico de N001 (5 muestras) ---"
./bin/cliente localhost "$SRV_PORT" admin admin123 historico N001 5 | grep -c "^#" \
    | sed "s/^/muestras devueltas: /"

echo
echo "### 5. Cliente OPERADOR (operador) ###"
./bin/cliente localhost "$SRV_PORT" operador oper123 resumen | grep -E "Autenticado|RESUMEN"

# 6) Casos de error
echo
echo "### 6. Control de errores ###"
echo "--- credenciales invalidas (espera rechazo) ---"
./bin/cliente localhost "$SRV_PORT" admin CLAVE_MALA resumen 2>&1 | grep -E "rechazada|401"
echo "--- consulta de nodo inexistente (espera ERROR 409) ---"
./bin/cliente localhost "$SRV_PORT" admin admin123 estado NODO_FANTASMA 2>&1 | grep -E "ERROR|409"

# 7) Deteccion de inactividad (resumen del log)
echo
echo "### 7. Resumen de actividad en el log del servidor ###"
echo "AUTH_REQ:  $(grep -c 'tipo=AUTH_REQ'  "$LOG")"
echo "REG_REQ:   $(grep -c 'tipo=REG_REQ'   "$LOG")"
echo "TELEMETRY: $(grep -c 'tipo=TELEMETRY' "$LOG")"
echo "EVENT:     $(grep -c 'tipo=EVENT '    "$LOG")"
echo "QUERY_REQ: $(grep -c 'tipo=QUERY_REQ' "$LOG")"
echo "ERROR:     $(grep -c 'tipo=ERROR'     "$LOG")"

echo
echo "Demo completada. Log del servidor: $LOG"
echo "(Los procesos se detienen automaticamente al salir.)"
