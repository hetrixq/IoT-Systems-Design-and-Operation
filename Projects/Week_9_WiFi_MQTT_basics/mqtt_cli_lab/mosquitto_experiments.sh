#!/usr/bin/env sh
set -eu

# Week 9 / 4.2
# Replace USER_ID and broker settings before running.
USER_ID="${USER_ID:-student01}"
BROKER_HOST="${BROKER_HOST:-127.0.0.1}"
BROKER_PORT="${BROKER_PORT:-1883}"
BASE_TOPIC="iot_practice/${USER_ID}"

echo "Broker: ${BROKER_HOST}:${BROKER_PORT}"
echo "Base topic: ${BASE_TOPIC}"

echo
echo "[1] Topic case sensitivity"
echo "Expected: subscriptions are case-sensitive, only matching case receives messages."
echo "Terminal A:"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/case/Test'"
echo "Terminal B:"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/case/test' -m 'lowercase'"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/case/Test' -m 'exact'"

echo
echo "[2] Wildcard in publish topic (negative)"
echo "Expected: broker rejects publish to wildcard topic."
echo "Run:"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/wild/#' -m 'invalid'"

echo
echo "[3] Leading and trailing slash behavior"
echo "Expected: '/x', 'x', and 'x/' are different topic strings."
echo "Terminal A:"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '/${BASE_TOPIC}/slash'"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/slash/'"
echo "Terminal B:"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '/${BASE_TOPIC}/slash' -m 'leading'"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/slash/' -m 'trailing'"

echo
echo "[4] stdin publish with -l"
echo "Expected: each line from stdin becomes separate MQTT message."
echo "Terminal A:"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/stream'"
echo "Terminal B:"
echo "  printf 'line-1\\nline-2\\nline-3\\n' | mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/stream' -l"

echo
echo "[5] Retained message behavior"
echo "Expected: new subscriber instantly receives retained message."
echo "Run:"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/retain' -m 'retained-state' -r"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/retain' -C 1 -v"

echo
echo "[6] Last Will and Testament"
echo "Expected: if client disconnects ungracefully, LWT appears on status topic."
echo "Terminal A:"
echo "  mosquitto_sub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/status'"
echo "Terminal B:"
echo "  mosquitto_pub -h ${BROKER_HOST} -p ${BROKER_PORT} -t '${BASE_TOPIC}/heartbeat' -m 'online' \\"
echo "    --will-topic '${BASE_TOPIC}/status' --will-payload 'offline' --will-qos 1 -d"
