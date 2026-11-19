#!/bin/sh
# Generates the mosquitto password file from the bench environment and starts
# the broker.  Re-running the container refreshes the hashes; the data volume
# keeps only hashes, never the plaintext password.
set -eu

: "${OKO_MQTT_USER:=oko}"
: "${OKO_MQTT_PASS:=oko}"
: "${OKO_MQTT_REC_USER:=oko-rec}"
: "${OKO_MQTT_REC_PASS:=oko-rec}"

mkdir -p /mosquitto/data
mosquitto_passwd -c -b /mosquitto/data/mosquitto.passwd \
    "$OKO_MQTT_USER" "$OKO_MQTT_PASS"
mosquitto_passwd -b /mosquitto/data/mosquitto.passwd \
    "$OKO_MQTT_REC_USER" "$OKO_MQTT_REC_PASS"
chown mosquitto:mosquitto /mosquitto/data/mosquitto.passwd 2>/dev/null || true
chmod 600 /mosquitto/data/mosquitto.passwd

exec /usr/sbin/mosquitto -c /mosquitto/config/mosquitto.conf