#!/bin/bash

systemctl disable readsbmqtt
systemctl stop readsbmqtt

rm -v -f /lib/systemd/system/readsbmqtt.service /etc/default/readsbmqtt /usr/bin/readsbmqtt