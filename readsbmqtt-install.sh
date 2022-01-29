#!/bin/bash
set -e

DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
BIN=/usr/bin/readsbmqtt
DEP="git build-essential libprotobuf-c-dev protobuf-c-compiler"
PAHO="https://github.com/eclipse/paho.mqtt.c"

apt install -y $DEP || apt update && apt install -y $DEP || true

rm -rf /tmp/paho.mqtt.c && git clone --depth 1 --branch master "$PAHO" /tmp/paho.mqtt.c

if [ ! -d "/tmp/paho.mqtt.c" ] 
then
    echo "Error cloning paho.mqtt.c repositiory!"
    exit 1
fi

cd /tmp/paho.mqtt.c && make && make install && ldconfig

cd "$DIR"
make

if [ -f "readsbmqtt" ]
then
    cp readsbmqtt.service /lib/systemd/system
    cp -n readsbmqtt.default /etc/default/readsbmqtt

    if ! getent passwd readsb >/dev/null
    then
        adduser --system --home /usr/share/$NAME --no-create-home --quiet readsb
    fi

    rm -f $BIN
    cp -T readsbmqtt $BIN

    systemctl enable readsbmqtt
    systemctl restart readsbmqtt
else
    echo "Error building readsbmqtt!"
    exit 1
fi