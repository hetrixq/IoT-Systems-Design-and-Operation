#!/usr/bin/env sh
set -eu

# Fill values before running.
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-1883}"
USER_ID="${USER_ID:-student01}"
MQTT_USER="${MQTT_USER:-user}"
MQTT_PASS="${MQTT_PASS:-pass}"

python3 sensor_client.py \
  --host "${HOST}" \
  --port "${PORT}" \
  --user-id "${USER_ID}" \
  --username "${MQTT_USER}" \
  --password "${MQTT_PASS}" \
  --interval 5

# Example (run separately):
# python3 skynet_publish.py \
#   --host "${HOST}" --port "${PORT}" \
#   --user-id "${USER_ID}" \
#   --username "${MQTT_USER}" --password "${MQTT_PASS}" \
#   --servers alpha beta gamma
