# Readsb MQTT statistics client

An MQTT client that reads statistics in protocol-buffer format from readsb ADS-B decoder and forward them via MQTT broker into home assistant (HASS).

## Build dependencies

* build-essential
* libprotobuf-c-dev
* protobuf-c-compiler
* libpaho-mqtt

For build instructions of libpaho-mqtt see https://github.com/eclipse/paho.mqtt.c

Build with `make`. See `readsbmqtt --help` for program options.

MQTT broker like mosquitto requires connection with username and password. Entities will be automatically discoverded in home assistant with default topic prefix `homeassistant/sensor`.