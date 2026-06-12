#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

echo "[1/4] Checking required project paths..."
for p in \
  "$ROOT_DIR/Week_9_WiFi_MQTT_basics/wifi_connect/main/main.c" \
  "$ROOT_DIR/Week_9_WiFi_MQTT_basics/mqtt_cli_lab/mosquitto_experiments.sh" \
  "$ROOT_DIR/Week_9_WiFi_MQTT_basics/mqtt_clients_lab/topic_contract.txt" \
  "$ROOT_DIR/Week_10_MQTT_devices/python_mqtt_client/sensor_client.py" \
  "$ROOT_DIR/Week_10_MQTT_devices/python_mqtt_client/skynet_publish.py" \
  "$ROOT_DIR/Week_10_MQTT_devices/mqtt_lamp/main/main.c" \
  "$ROOT_DIR/Week_10_MQTT_devices/dynamic_lighting/main/main.c"
do
  if [ ! -f "$p" ]; then
    echo "Missing required file: $p" >&2
    exit 1
  fi
done
echo "OK: required files are present."

echo "[2/4] Checking python syntax..."
python3 -m py_compile \
  "$ROOT_DIR/Week_10_MQTT_devices/python_mqtt_client/sensor_client.py" \
  "$ROOT_DIR/Week_10_MQTT_devices/python_mqtt_client/skynet_publish.py"
echo "OK: python syntax check passed."

echo "[3/4] Checking topic prefix usage..."
if ! rg -n "iot_practice/" "$ROOT_DIR/Week_9_WiFi_MQTT_basics" "$ROOT_DIR/Week_10_MQTT_devices" >/dev/null; then
  echo "No iot_practice topics found." >&2
  exit 1
fi
echo "OK: topic prefix present."

echo "[4/4] ESP-IDF tool availability..."
if command -v idf.py >/dev/null 2>&1; then
  echo "idf.py found. You can run builds manually for each ESP-IDF project."
else
  echo "idf.py not found in PATH. ESP-IDF build check skipped."
fi

echo "Smoke check completed."
