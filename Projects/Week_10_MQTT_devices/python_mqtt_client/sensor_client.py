#!/usr/bin/env python3
import argparse
import json
import random
import time

import paho.mqtt.client as mqtt


def parse_args():
    parser = argparse.ArgumentParser(description="Week10/4.4 sensor MQTT client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user-id", required=True)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--interval", type=float, default=5.0)
    return parser.parse_args()


def on_connect(client, userdata, flags, rc, properties=None):
    user_id = userdata["user_id"]
    topic = f"iot_practice/{user_id}/servers/#"
    client.subscribe(topic)
    print(f"[connect] rc={rc}, subscribed: {topic}")


def on_message(client, userdata, msg):
    payload = msg.payload.decode(errors="replace")
    print(f"[msg] {msg.topic} -> {payload}")


def build_sensor_payload():
    return {
        "temperature": round(random.uniform(18.0, 28.0), 1),
        "humidity": round(random.uniform(35.0, 70.0), 1),
        "luminosity": random.randint(100, 950),
        "ts": int(time.time()),
    }


def main():
    args = parse_args()
    base = f"iot_practice/{args.user_id}"
    pub_topic = f"{base}/sensors"

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, userdata={"user_id": args.user_id})
    client.username_pw_set(args.username, args.password)
    client.on_connect = on_connect
    client.on_message = on_message

    client.connect(args.host, args.port, keepalive=60)
    client.loop_start()

    try:
        while True:
            payload = build_sensor_payload()
            payload_str = json.dumps(payload, separators=(",", ":"))
            result = client.publish(pub_topic, payload_str, qos=1, retain=False)
            print(f"[pub] topic={pub_topic}, mid={result.mid}, payload={payload_str}")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("Stopping client...")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
