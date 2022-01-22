// Part of readsbmqtt, an MQTT client that reads statistics from readsb
// ADS-B decoder and forward them via MQTT broker into home assistant (HASS)
//
// readsmqtt.h: Readsb MQTT client. (header)
//
// Copyright (c) 2022 Michael Wolf <michael@mictronics.de>
//
// This file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef READSBMQTT_H
#define READSBMQTT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <argp.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <stdint.h>
#include <fcntl.h>
#include <MQTTClient.h>
#include "readsb.pb-c.h"

static const char *READSB_STATS_FILE_PB = "/run/readsb/stats.pb";

#define NOTUSED(V) ((void) V)

#define QOS         1
#define TIMEOUT     10000L
#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

// For string length limitations see MQTT v3.1.1, the connect packet
#define MAX_URI_SIZE        65535
#define MAX_AUTH_SIZE       65535
#define MAX_PAYLOAD_SIZE    65535
#define MAX_ID_SIZE         23
#define MAX_TOPIC_SIZE      250

const char *argp_program_bug_address = "";
static error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct argp_option options[] = {
    {"broker", 'b', "<URI>", 0, "MQTT broker URI (default: tcp://localhost:1883)", 1},
    {"user", 'u', "<username>", 0, "MQTT broker auth username", 1},
    {"pass", 'p', "<password>", 0, "MQTT broker auth password", 1},
    {"id", 'i', "<clientid>", 0, "MQTT unique client id (default: feeder001)", 1},
    {"topic", 't', "<topic>", 0, "MQTT topic prefix (default: homeassistant/sensor)", 1},
    { 0}
};

static const char *MQTT_SENSOR_CONFIG =
        "{"
        "\"name\":\"%s %s\","
        "\"unique_id\":\"%s.%s\","
        "\"state_topic\":\"%s/%s/properties\","
        "\"val_tpl\":\"{{value_json.%s}}\","
        "\"icon\":\"%s\","
        "\"platform\":\"mqtt\","
        "\"unit_of_measurement\":\"%s\""
        "}\0";

static const char *MQTT_STATUS_CONFIG =
        "{"
        "\"name\":\"%s Status\","
        "\"unique_id\":\"%s.running\","
        "\"device_class\": \"running\","
        "\"state_topic\":\"%s/%s/properties\","
        "\"val_tpl\":\"{{value_json.running}}\","
        "\"payload_on\":\"1\","
        "\"payload_off\":\"0\","
        "\"platform\":\"mqtt\""
        "}\0";

// HASS auto discover: <discovery_prefix>/<component>/[<node_id>/]<object_id>/config
static const char *MQTT_TOPIC_CONFIG = "%s/%s/%s/config\0";
static const char *MQTT_TOPIC_PROPERTIES = "%s/%s/properties\0";

static struct {
    const char *id;
    const char *name;
    const char *unit;
    double val;
} statistics[] = {
    {"messages", "Messages", "Messages", 0},
    {"tracks_new", "Tracking", "Aircraft", 0},
    {"tracks_single", "Single", "Aircraft", 0},
    {"tracks_mlat", "MLAT", "Aircraft", 0},
    {"tracks_position", "Positions", "Aircraft", 0},
    {"max_dist_metric", "Maximum Distance Metric", "km", 0},
    {"max_dist_imp", "Maximum Distance Imperial", "nm", 0},
    {"local_strong", "Strong Signals", "Messages", 0},
    {"local_signal", "Signal", "dBFS", 0},
    {"local_noise", "Noise", "dBFS", 0},
    {"local_peak", "Peak", "dBFS", 0},
    {"temperatur", "Temperature", "°C", 0},
    {NULL, NULL, NULL, 0}
};

#endif /* READSBMQTT_H */

