// Part of readsbmqtt, an MQTT client that reads statistics from readsb
// ADS-B decoder and forward them via MQTT broker into home assistant (HASS)
//
// readsmqtt.c: Readsb MQTT client.
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

#include "readsbmqtt.h"

static int app_exit = 0;
static int app_return_code = EXIT_SUCCESS;
static int new_stats = 0;
static uint64_t last_timestamp = 0;
static int feeder_status = 0;
static int inotify_fd;
static error_t parse_opt(int key, char *arg, struct argp_state *state);
const char *argp_program_version = "readsbmqtt v1.0.0";
const char doc[] = "Readsb MQTT statistics client";
const char args_doc[] = "";
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

MQTTClient_deliveryToken delivered_token;
MQTTClient_connectOptions connect_options = MQTTClient_connectOptions_initializer;
static char *server_uri;
static char *client_id;
static char *topic_prefix;
static char payload[MAX_PAYLOAD_SIZE];

/**
 * Signal handler
 * @param sig Signal number we got.
 */
static void signal_handler(int sig) {
    signal(sig, SIG_DFL); // Reset signal handler
    app_exit = 1;
    app_return_code = EXIT_SUCCESS;
    fprintf(stderr, "caught signal %s, shutting down..\n", strsignal(sig));
}

/**
 * Command line option parser.
 * @param key Option key.
 * @param argc number of command line arguments.
 * @param argv command line arguments.
 * @param state PArsing state.
 * @return Command line options have error, or not.
 */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    switch (key) {
        case 'b':
            server_uri = strndup(arg, MAX_URI_SIZE);
            break;
        case 'u':
            connect_options.username = strndup(arg, MAX_AUTH_SIZE);
            break;
        case 'p':
            connect_options.password = strndup(arg, MAX_AUTH_SIZE);
            break;
        case 'i':
            client_id = strndup(arg, MAX_ID_SIZE);
            break;
        case 't':
            topic_prefix = strndup(arg, MAX_TOPIC_SIZE);
            break;
        case ARGP_KEY_END:
            if (state->arg_num > 0)
                /* We use only options but no arguments */
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/**
 * Message delivered callback.
 * @param context Application specific context
 * @param dt Delivery token
 */
static void msg_delivered(void *context, MQTTClient_deliveryToken dt) {
    NOTUSED(context);
    delivered_token = dt;
}

/**
 * Messager arrived callback.
 * @param context Application specific context
 * @param topic_name Message topic
 * @param topic_length Message topic length
 * @param message Arrived message.
 * @return 
 */
static int msg_arrived(void *context, char *topic_name, int topic_length, MQTTClient_message *message) {
    NOTUSED(context);
    NOTUSED(topic_length);
    fprintf(stderr, "got message\ntopic %s\n", topic_name);
    fprintf(stderr, "payload %.*s\n", message->payloadlen, (char*) message->payload);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic_name);
    return 1;
}

/**
 * Connection lost callback
 * @param context Application specific context
 * @param cause of connection lost
 */
static void connection_lost(void *context, char *cause) {
    NOTUSED(context);
    fprintf(stderr, "connection lost: %s\n", cause);
    app_exit = 1;
    app_return_code = EXIT_FAILURE;
}

/**
 * Read and process readsb stats.pb file.
 * @param file_name Absolute path and file name.
 */
static void update_from_stats(const char *file_name) {
    struct stat st;
    off_t file_size = 0;
    static uint8_t *read_buf;
    static Statistics *stats_msg;

    if (stat(file_name, &st) == 0) {
        file_size = st.st_size;
    } else {
        fprintf(stderr, "cannot determine size of %s: %s\n", file_name, strerror(errno));
        return;
    }

    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "cannot open file %s: %s\n", file_name, strerror(errno));
        return;
    }

    read_buf = (uint8_t *) malloc(file_size);
    if (read_buf == NULL) {
        fprintf(stderr, "unable to allocated read buffer for %s\n", file_name);
        close(fd);
        return;
    }

    file_size = read(fd, read_buf, file_size);
    close(fd);

    if (file_size == 0) {
        return;
    }

    stats_msg = statistics__unpack(NULL, file_size, read_buf);
    free(read_buf);
    if (stats_msg == NULL) {
        fprintf(stderr, "unpacking statistics message failed\n");
        return;
    }

    if (stats_msg->last_1min->stop - last_timestamp > 90) {
        feeder_status = 0;
    } else {
        feeder_status = 1;
    }
    last_timestamp = stats_msg->last_1min->stop;
    statistics[0].val = (double) stats_msg->last_1min->messages;
    statistics[1].val = (double) stats_msg->last_1min->tracks_new;
    statistics[2].val = (double) stats_msg->last_1min->tracks_single_message;
    statistics[3].val = (double) stats_msg->last_1min->tracks_mlat_position;
    statistics[4].val = (double) stats_msg->last_1min->tracks_with_position;
    statistics[5].val = (double) stats_msg->last_1min->max_distance_in_metres / 1000;
    statistics[6].val = (double) stats_msg->last_1min->max_distance_in_nautical_miles;
    statistics[7].val = (double) stats_msg->last_1min->local_strong_signals;
    statistics[8].val = (double) stats_msg->last_1min->local_signal;
    statistics[9].val = (double) stats_msg->last_1min->local_noise;
    statistics[10].val = (double) stats_msg->last_1min->local_peak_signal;
    statistics__free_unpacked(stats_msg, NULL);

    fd = open("/sys/class/hwmon/hwmon0/temp1_input", O_RDONLY);
    char buf[10] = {0};
    float temp;
    if (read(fd, buf, 10)) {
        sscanf(buf, "%f", &temp);
        statistics[11].val = (double) (temp / 1000);
    }
    close(fd);
}

/**
 * Signal IO handler
 * @param sig Signal number we got
 */
static void signal_io_handler(int sig) {
    NOTUSED(sig);
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    ssize_t numRead = read(inotify_fd, buf, BUF_LEN);
    /* Process all of the events in buffer returned by read() */
    for (char *p = buf; p < buf + numRead;) {
        struct inotify_event *event = (struct inotify_event *) p;
        if (strcmp(event->name, "stats.pb") == 0) {
            // We got a new stats.pb from temp file
            if (event->mask & IN_MOVED_TO) {
                update_from_stats(READSB_STATS_FILE_PB);
                new_stats = 1;
            }
            // stats.pb deleted, readsb stopped?
            if (event->mask & IN_DELETE) {
                fprintf(stderr, "error stats.pb deleted. readsb stopped?\n");
                app_exit = 1;
                app_return_code = EXIT_FAILURE;
            }
        }
        p += sizeof (struct inotify_event) +event->len;
    }
}

int main(int argc, char* argv[]) {
    MQTTClient client;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_willOptions lwt_options = MQTTClient_willOptions_initializer;
    MQTTClient_deliveryToken token;
    int len, mqtt_rc;
    char topic[MAX_TOPIC_SIZE];

    // General signal handlers:
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGABRT, signal_handler);

    // Set defaults
    server_uri = strdup("tcp://localhost:1883");
    client_id = strdup("feeder001");
    topic_prefix = strdup("homeassistant/sensor");

    // Create last will: client not running
    snprintf(topic, MAX_TOPIC_SIZE, MQTT_TOPIC_PROPERTIES, topic_prefix, client_id);
    lwt_options.topicName = topic;
    lwt_options.message = "{\"running\": \"0\"}\0";
    lwt_options.qos = QOS;

    // Parse the command line options
    if (argp_parse(&argp, argc, argv, 0, 0, 0)) {
        return EXIT_FAILURE;
    }

    if ((mqtt_rc = MQTTClient_create(&client, server_uri, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "create client error: %d\n", mqtt_rc);
        app_return_code = EXIT_FAILURE;
        goto exit;
    }

    if ((mqtt_rc = MQTTClient_setCallbacks(client, NULL, connection_lost, msg_arrived, msg_delivered)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "set callbacks error: %d\n", mqtt_rc);
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    connect_options.keepAliveInterval = 20;
    connect_options.cleansession = 1;
    connect_options.will = &lwt_options;
    if ((mqtt_rc = MQTTClient_connect(client, &connect_options)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "connect error: %d\n", mqtt_rc);
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Add notification on stats file when connected to MQTT broker
    // Create inotify instance
    inotify_fd = inotify_init();
    if (inotify_fd == -1) {
        fprintf(stderr, "inotify_init error");
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Establish handler for "I/O possible" signal
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = signal_io_handler;
    if (sigaction(SIGIO, &sa, NULL) == -1) {
        fprintf(stderr, "sigaction error");
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Set owner process that is to receive IO signal
    if (fcntl(inotify_fd, F_SETOWN, getpid()) == -1) {
        fprintf(stderr, "F_SETOWN error");
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Enable IO signaling and make I/O nonblocking for file descriptor
    int flags = fcntl(inotify_fd, F_GETFL);
    if (fcntl(inotify_fd, F_SETFL, flags | O_ASYNC | O_NONBLOCK) == -1) {
        fprintf(stderr, "F_SETFL error");
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Add notification watch to readsb stats file when closed after write.
    // Notify also when stats.pb gets deleted (readsb stopped).
    int inotify_wd = inotify_add_watch(inotify_fd, "/run/readsb/", IN_MOVED_TO | IN_DELETE);
    if (inotify_wd == -1) {
        fprintf(stderr, "inotify_add_watch error: readsb running? ");
        app_return_code = EXIT_FAILURE;
        goto destroy_exit;
    }

    // Run this until we get a termination signal.
    while (!app_exit && MQTTClient_isConnected(client)) {
        MQTTClient_yield();
        // Wait for new statistics
        if (new_stats) {
            new_stats = 0;
            // Sensor config needs to be send frequently otherwise HASS will not recognize sensors after
            // HASS server restart. (there is no connection loss since we are connected to MQTT broker)
            for (int f = 0; statistics[f].name; ++f) {
                // Create topic configuration
                snprintf(topic, MAX_TOPIC_SIZE, MQTT_TOPIC_CONFIG, topic_prefix, client_id, statistics[f].id);
                // Create json payload
                len = snprintf(payload, MAX_PAYLOAD_SIZE, MQTT_SENSOR_CONFIG,
                        client_id, // name part 1
                        statistics[f].name, // name part 2
                        client_id, // unique id part 1
                        statistics[f].id, // unique id part 2
                        topic_prefix, // state topic part 1
                        client_id, // state topic part 2
                        statistics[f].id, // value template name
                        "mdi:airplane", // icon name
                        statistics[f].unit // unit of measure
                        );

                pubmsg.payload = payload;
                pubmsg.payloadlen = len;
                pubmsg.qos = QOS;
                pubmsg.retained = 0;
                delivered_token = 0;
                if ((mqtt_rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
                    fprintf(stderr, "publish stats config error: %d\n", mqtt_rc);
                    app_return_code = EXIT_FAILURE;
                } else {
                    MQTTClient_waitForCompletion(client, delivered_token, 100);
                }
            }

            // Create feeder status config
            snprintf(topic, MAX_TOPIC_SIZE, MQTT_TOPIC_CONFIG, "homeassistant/binary_sensor", client_id, "running");
            // Create json payload
            len = snprintf(payload, MAX_PAYLOAD_SIZE, MQTT_STATUS_CONFIG,
                    client_id, // name part 1
                    client_id, // unique id part 1
                    topic_prefix, // state topic part 1
                    client_id // state topic part 2
                    );

            pubmsg.payload = payload;
            pubmsg.payloadlen = len;
            pubmsg.qos = QOS;
            pubmsg.retained = 0;
            delivered_token = 0;
            if ((mqtt_rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
                fprintf(stderr, "publish status config error: %d\n", mqtt_rc);
                app_return_code = EXIT_FAILURE;
            } else {
                MQTTClient_waitForCompletion(client, delivered_token, 100);
            }

            // Create properties topic
            snprintf(topic, MAX_TOPIC_SIZE, MQTT_TOPIC_PROPERTIES, topic_prefix, client_id);
            // Create properties json payload
            char buf[100];
            char *p = payload;
            snprintf(payload, MAX_PAYLOAD_SIZE, "{\"%s\": \"%0.1lf\"", statistics[0].id, statistics[0].val);
            for (int g = 1; statistics[g].name; ++g) {
                snprintf(buf, 100, ", \"%s\": \"%0.1lf\"", statistics[g].id, statistics[g].val);
                p = strcat(p, buf);
            }
            // Add feeder status
            snprintf(buf, 100, ", \"running\": \"%u\"", feeder_status);
            p = strcat(p, buf);
            strcat(p, "}\0");

            pubmsg.payload = payload;
            pubmsg.payloadlen = (int) strlen(payload);
            pubmsg.qos = QOS;
            pubmsg.retained = 0;
            delivered_token = 0;
            if ((mqtt_rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
                fprintf(stderr, "publish properties error: %d\n", mqtt_rc);
                app_return_code = EXIT_FAILURE;
            } else {
                MQTTClient_waitForCompletion(client, delivered_token, 100);
            }
        }
    }

    // Publish client not running status on _expected_ disconnect
    // Last will is send only on _unexpected_ disconnect.
    if (MQTTClient_isConnected(client)) {
        snprintf(topic, MAX_TOPIC_SIZE, MQTT_TOPIC_PROPERTIES, topic_prefix, client_id);
        snprintf(payload, MAX_PAYLOAD_SIZE, "{\"running\": \"0\"}");
        pubmsg.payload = payload;
        pubmsg.payloadlen = (int) strlen(payload);
        pubmsg.qos = QOS;
        pubmsg.retained = 0;
        delivered_token = 0;
        if ((mqtt_rc = MQTTClient_publishMessage(client, topic, &pubmsg, &token)) != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "publish disconnect error: %d\n", mqtt_rc);
        } else {
            MQTTClient_waitForCompletion(client, delivered_token, 100);
        }
    }

    if ((mqtt_rc = MQTTClient_disconnect(client, 1000)) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "disconnect error: %d\n", mqtt_rc);
        app_return_code = EXIT_FAILURE;
    }

destroy_exit:
    MQTTClient_destroy(&client);

exit:
    free(server_uri);
    free(client_id);
    free(topic_prefix);
    if (inotify_fd && inotify_wd) {
        inotify_rm_watch(inotify_fd, inotify_wd);
    }
    return app_return_code;
}