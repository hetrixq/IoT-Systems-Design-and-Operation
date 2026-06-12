#!/usr/bin/env python3
import argparse
import hashlib

import paho.mqtt.client as mqtt


def parse_args():
    parser = argparse.ArgumentParser(description="Week10/4.4 SkyNet code publisher")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user-id", required=True)
    parser.add_argument("--username", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--servers", required=True, nargs="+")
    return parser.parse_args()


def skynet_code(server, user_id):
    return hashlib.md5(f"{server}:{user_id}".encode("utf-8")).hexdigest()[:6]


def main():
    args = parse_args()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(args.username, args.password)
    client.connect(args.host, args.port, keepalive=60)
    client.loop_start()

    try:
        for server in args.servers:
            code = skynet_code(server, args.user_id)
            topic = f"iot_practice/{args.user_id}/servers/{server}"
            result = client.publish(topic, code, qos=1, retain=False)
            print(f"[pub] topic={topic}, code={code}, mid={result.mid}")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
