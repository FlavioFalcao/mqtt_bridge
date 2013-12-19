/*
* The MIT License (MIT)
*
* Copyright (c) 2013, Marcelo Aquino, https://github.com/mapnull
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <time.h>
#include <sys/time.h>

#include <mosquitto.h>

#include "mqtt_bridge.h"
#include "utils.h"
#include "arduino-serial-lib.h"
#include "device.h"
#include "serial.h"
#include "netdev.c"

#define MICRO_PER_SECOND	1000000.0
#define SERIAL_MAX_BUF 100
#define MAX_OUTPUT 256
#define GBUF_SIZE 100

const char version[] = "0.0.1";

struct bridge bridge;

const char eolchar = '\n';

static int run = 1;
static int user_signal = false;
static bool bandwidth = false;
struct bridge_config config;
static double downspeed, upspeed;
static bool every30s = false;
static bool quiet = false;
static bool connected = true;

char gbuf[GBUF_SIZE + 1];

void handle_signal(int signum)
{
	double drift;
	static bool sigUSR1_flag = false;
	static struct timeval sigUSR1;
	static struct timeval sigUSR2;

	if (config.debug > 1) printf("Signal: %d\n", signum);

	if (signum == SIGUSR1) {
		sigUSR1_flag = true;
		gettimeofday(&sigUSR1, NULL);
		return;
	}
	else if(signum == SIGUSR2) {
		if (!sigUSR1_flag) {
			if (config.debug > 1) printf("SIGUSR2 before SIGUSR1.\n");
			return;
		}
		sigUSR1_flag = false;
		gettimeofday(&sigUSR2, NULL);
		drift = ((sigUSR2.tv_sec - sigUSR1.tv_sec) + ((sigUSR2.tv_usec - sigUSR1.tv_usec)/MICRO_PER_SECOND));

		if (drift > 2.0)
			user_signal = MODULE_SIGUSR2;
		else
			user_signal = MODULE_SIGUSR1;
		if (config.debug > 2) printf("user_signal drift: %f\n", drift);
		return;
	}
    run = 0;
}

void each_sec(int x)
{
	static int cnt = 0;
	static struct timeval t1, t2;
	static int seconds = 0;
	double drift;
	static unsigned long long int oldrec, oldsent, newrec, newsent;
	
	if (bandwidth) {
		if (cnt == 0) {
			gettimeofday( &t1, NULL );
			if (parse_netdev(&newrec, &newsent, config.interface)) {
				fprintf(stderr, "Error when parsing /proc/net/dev file.\n");
				exit(1);
			}
			cnt++;
			alarm(1);
			return;
		}

		oldrec = newrec;
		oldsent = newsent;
		if (parse_netdev(&newrec, &newsent, config.interface)) {
			fprintf(stderr, "Error when parsing /proc/net/dev file.\n");
			exit(1);
		}

		if (cnt % 2 == 0) {		// Even
			gettimeofday( &t1, NULL );
			drift=(t1.tv_sec - t2.tv_sec) + ((t1.tv_usec - t2.tv_usec)/MICRO_PER_SECOND);
		} else {				// Odd
			gettimeofday( &t2, NULL );
			drift=(t2.tv_sec - t1.tv_sec) + ((t2.tv_usec - t1.tv_usec)/MICRO_PER_SECOND);
		}
		if (config.debug > 3) printf("%.6lf seconds elapsed\n", drift);

		downspeed = (newrec - oldrec) / drift / 128.0;		// Kbits = / 128; KBytes = / 1024
		upspeed = (newsent - oldsent) / drift / 128.0;		// Kbits = / 128; KBytes = / 1024
		
		cnt++;
	}

	if (++seconds == 60)
		seconds = 0;
	if (seconds % 30 == 0) every30s = true;

	if (config.debug > 3) printf("seconds: %d\n", seconds);
	alarm(1);
}

int mqtt_publish(struct mosquitto *mosq, char *topic, char *payload)
{
	int rc;

	rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, config.mqtt_qos, false);
	if (rc) {
		fprintf(stderr, "Error: MQTT publish returned: %s\n", mosquitto_strerror(rc));
		return 0;
	}
	return 1;
}

int mqtt_publish_bandwidth(struct mosquitto *mosq, char *topic) {
	int payload_len;
	char *payload;

	if (!topic)
		return 1;

	if (config.debug > 1) printf("down: %f - up: %f\n", downspeed, upspeed);

	payload_len = snprintf(NULL, 0, "%.0f,%.0f", upspeed, downspeed);
	if ((payload = (char *)malloc((payload_len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}
	snprintf(payload, payload_len + 1, "%.0f,%.0f", upspeed, downspeed);
	mqtt_publish(mosq, topic, payload);
	free(payload);

	return 0;
}

void on_mqtt_connect(struct mosquitto *mosq, void *obj, int result)
{
	int rc;

	if (!result) {
		connected = true;
		if(config.debug != 0) printf("MQTT Connected.\n");

		rc = mosquitto_subscribe(mosq, NULL, bridge.config_topic, config.mqtt_qos);
		if (rc) {
			fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
			run = 0;
			return;
		}
		snprintf(gbuf, GBUF_SIZE, "%d,%d", PROTO_ST_ALIVE, bridge.modules_len);
		mqtt_publish(mosq, bridge.status_topic, gbuf);
		return;
	} else {
		fprintf(stderr, "MQTT - Failed to connect: %s\n", mosquitto_connack_string(result));
    }
}

void on_mqtt_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	connected = false;
	bridge.controller = false;
	if (config.debug != 0) printf("MQTT Disconnected: %s\n", mosquitto_strerror(rc));
}

void bridge_message(struct mosquitto *mosq, int sd, struct device *dev, char *msg)
{
	char md_id[DEVICE_MD_ID_SIZE + 1];
	struct module *md;
	struct device *target_dev;
	int code, i;

	if (config.debug > 2) printf("Bridge - message: %s\n", msg);

	if (!getInt(&msg, &code)) {
		if (config.debug > 1) printf("MQTT - Invalid data.\n");
		return;
	}

	switch (code) {
		case PROTO_ERROR:
		case PROTO_ACK:
		case PROTO_NACK:
		case PROTO_ST_TIMEOUT:
		case PROTO_DEVICE:
			return;
		case PROTO_ST_ALIVE:
			if (!getInt(&msg, &code))
				return;
			if (dev->modules == code)
				return;
			dev->modules = code;
			// no return here is purposeful
		case PROTO_ST_MODULES_UP:	// Modules Update
			if (dev->type == DEVICE_TYPE_NODE) {
				// Message from a serial device
				if (dev->md_deps->type == MODULE_SERIAL && bridge.serial_ready) {
					snprintf(gbuf, GBUF_SIZE, "%s%s,%d", SERIAL_INIT_MSG, dev->id, PROTO_GET_MODULES);
					serialport_printlf(sd, gbuf);
				}
				// Message from a MQTT device
				else if (dev->md_deps->type == MODULE_MQTT) {
					snprintf(gbuf, GBUF_SIZE, "%s,%d", bridge.id, PROTO_GET_MODULES);
					mqtt_publish(mosq, dev->topic, gbuf);
					return;
				}
			}
			return;
		case PROTO_GET_MODULES:
			// Message from a MQTT device
			if (dev->md_deps->type == MODULE_MQTT) {
				for (md = bridge.module; md != NULL; md = md->next) {
					snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%s,%d", bridge.id, PROTO_MODULE, md->id, md->device, md->enabled);
					mqtt_publish(mosq, dev->topic, gbuf);
				}
			}
			return;
		case PROTO_GET_DEVICES:
			// Message from a MQTT device
			if (dev->md_deps->type == MODULE_MQTT) {
				for (i = 0; i < bridge.devices_len; i++) {
					target_dev = &bridge.devices[i];
					snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%d,%d"
						, bridge.id, PROTO_DEVICE, target_dev->id, target_dev->modules, target_dev->alive);
					mqtt_publish(mosq, dev->topic, gbuf);
				}
			}
			return;
		case PROTO_SAVE_DEVICE:
			target_dev = device_get(&bridge, msg);
			if (!target_dev)
				return;
			if (config.debug > 1) {
				printf("Saving device:\n");
				device_print_device(target_dev);
			}
			device_save(&bridge, config.devices_folder, target_dev);
			return;
	}

	if (!getString(&msg, md_id, DEVICE_MD_ID_SIZE, ',')) {
		if (config.debug > 1) printf("Missing module id - code: %d\n", code);
		return;
	}
	if (!device_isValid_md_id(md_id)) {
		if (config.debug > 1) printf("Invalid module id - code: %d\n", code);
		return;
	}

	md = device_get_module(&bridge, md_id);
	if (code == PROTO_MODULE) {
		if (!md) {
			if (device_add_module(&bridge, md_id, dev->id) == -1) {
				run = 0;
				return;
			}
			if (config.debug > 1) {
				md = device_get_module(&bridge, md_id);
				printf("New Module:\n");
				device_print_module(md);
			}
		}
		return;
	}

	if (!md)
		return;
	target_dev = device_get(&bridge, md->device);
	if (!target_dev) {
		fprintf(stderr, "Error: Orphan module.\n");
		device_remove_module(&bridge, md_id);
		return;
	}

	switch (code) {
		case PROTO_GET_MODULE:
			// Message from a MQTT device
			if (dev->md_deps->type == MODULE_MQTT) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%s,%d", bridge.id, PROTO_MODULE, md->id, md->device, md->enabled);
				mqtt_publish(mosq, dev->topic, gbuf);
			}
			return;
		case PROTO_MD_GET_TOPIC:
			// Message from a MQTT device
			if (dev->md_deps->type == MODULE_MQTT) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%s", bridge.id, PROTO_MD_TOPIC, md->id, md->topic);
				mqtt_publish(mosq, dev->topic, gbuf);
			}
			return;
		case PROTO_MD_SET_TOPIC:
		case PROTO_MD_TOPIC:
			code = device_set_md_topic(md, msg);
			if (code == -1) {		// Memory problem
				run = 0;
				return;
			}
			if (code == 0) {		// Module topic changed
				snprintf(gbuf, GBUF_SIZE, "%d,%s,%s", PROTO_MD_TOPIC, md->id, md->topic);
				mqtt_publish(mosq, bridge.status_topic, gbuf);
			}
			return;
		case PROTO_MD_RAW:
			mqtt_publish(mosq, md->topic, msg);
			return;
		case PROTO_MD_TO_RAW:
			// Target module at serial
			if (target_dev->md_deps->type == MODULE_SERIAL && bridge.serial_ready) {
				snprintf(gbuf, GBUF_SIZE, "%s%s,%d,%s,%s", SERIAL_INIT_MSG, target_dev->id, PROTO_MD_TO_RAW, md->id, msg);
				serialport_printlf(sd, gbuf);
			}
			// Target module at MQTT
			else if (target_dev->md_deps->type == MODULE_MQTT) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%s", bridge.id, PROTO_MD_TO_RAW, md->id, msg);
				mqtt_publish(mosq, target_dev->topic, gbuf);
			}
			else if (!strcmp(md->device, bridge.id)) {
				if (md->type == MODULE_SCRIPT) {
					code = run_script(config.scripts_folder, msg, gbuf, GBUF_SIZE, config.debug);
					if (code == -1) {
						run = 0;
					}
					else if (code == 1) {
						mqtt_publish(mosq, md->topic, "0");	
					}
					else if (code == 0) {
						if (strlen(gbuf) > 0) {
							if (config.debug > 1) printf("Script output:\n-\n%s\n-\n", gbuf);
							mqtt_publish(mosq, md->topic, gbuf);
						} else {
							mqtt_publish(mosq, md->topic, "1");
						}
					}
				}
				else if (md->type == MODULE_BANDWIDTH) {
					if (bandwidth) {
						if (mqtt_publish_bandwidth(mosq, md->topic) == -1)
							run = 0;
					}
				}
				else if (md->type == MODULE_SERIAL) {
					snprintf(gbuf, GBUF_SIZE, "%d", bridge.serial_ready);
					mqtt_publish(mosq, md->topic, gbuf);
				}
			}
			return;
		case PROTO_MD_ENABLE:			//TODO: implement
		case PROTO_MD_GET_ENABLE:		//TODO: implement
		case PROTO_MD_SET_ENABLE: 		//TODO: implement
			return;
		default:
			if (config.debug > 2) printf("Bridge - code: %d - Not treated.\n", code);
	}
}

void on_mqtt_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *payload;
	int *sd;
	char id[DEVICE_ID_SIZE + 1];
	struct device *dev;
	int rc;

	sd = (int *)obj;
	payload  = (char *)msg->payload;

	if (config.debug > 2) printf("MQTT - topic: %s - payload: %s\n", msg->topic, payload);

	if (!strcmp(msg->topic, bridge.config_topic)) {
		if (getString(&payload, id, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
			if (config.debug > 1) printf("MQTT - Invalid data.\n");
			return;
		}
	} else {
		strncpy(id, &msg->topic[7], DEVICE_ID_SIZE);	// 7 - strlen("config/");
	}

	if (!device_isValid_id(id)) {
		if (config.debug > 1) printf("MQTT - Invalid device id.\n");
		return;
	}

	dev = device_get(&bridge, id);
	if (!dev) {
		rc = device_load(&bridge, config.devices_folder, id);
		if (rc == -1) {
			run = 0;
			return;
		}
		if (rc) {
			rc = device_add_dev(&bridge, id, MODULE_MQTT_ID);
			if (rc == -1) {
				run = 0;
				return;
			}
			if (rc) {
				if (config.debug > 2) printf("MQTT - Failed to add device.\n");
				return;
			}
		}
		dev = device_get(&bridge, id);
		if (config.debug > 1) printf("New device:\n");
		device_print_device(dev);
		if (dev->type == DEVICE_TYPE_NODE) {
			snprintf(gbuf, GBUF_SIZE, "status/%s", dev->id);
			rc = mosquitto_subscribe(mosq, NULL, gbuf, config.mqtt_qos);
			if (rc) {
				fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
				run = 0;
				return;
			}
		} else if (dev->type == DEVICE_TYPE_CONTROLLER) {
			bridge.controller = true;
		}
	} else
		dev->alive = ALIVE_CNT;

	bridge_message(mosq, *sd, dev, payload);
}

int serial_in(int sd, struct mosquitto *mosq, char *md_id)
{
	static char serial_buf[SERIAL_MAX_BUF];
	static int buf_len = 0;
	char *buf_p;
	char id[DEVICE_ID_SIZE + 1];
	struct device *dev;
	int rc, sread;

	if (buf_len)
		buf_p = &serial_buf[buf_len - 1];
	else
		buf_p = &serial_buf[0];

	sread = serialport_read_until(sd, buf_p, eolchar, SERIAL_MAX_BUF - buf_len, config.serial.timeout);
	if (sread == -1) {
		fprintf(stderr, "Serial - Read Error.\n");
		return -1;
	} 
	if (sread == 0)
		return 0;

	buf_len += sread;

	if (serial_buf[buf_len - 1] == eolchar) {
		serial_buf[buf_len - 1] = 0;		//replace eolchar
		buf_len--;					// eolchar was counted, decreasing 1
		if (config.debug > 3) printf("Serial - size:%d, serial_buf:%s\n", buf_len, serial_buf);
		if (buf_len < SERIAL_INIT_LEN) {	// We need at least SERIAL_INIT_LEN to count as a valid command
			if (config.debug > 1) printf("Invalid serial input.\n");
			buf_len = 0;
			return 0;
		}
		sread = buf_len;	// for return
		buf_len = 0;		// reseting for the next input

		buf_p = &serial_buf[SERIAL_INIT_LEN];

		// Serial debug
		if (!strncmp(serial_buf, SERIAL_INIT_DEBUG, SERIAL_INIT_LEN)) {
			if (config.debug) printf("Debug: %s\n", buf_p);
			return sread;
		}
		else if (!strncmp(serial_buf, SERIAL_INIT_MSG, SERIAL_INIT_LEN)) {
			if (config.debug > 2) printf("Serial - message: %s\n", serial_buf);

			if (getString(&buf_p, id, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
				if (config.debug > 1) printf("Serial - Invalid data.\n");
				return 0;
			}

			if (!device_isValid_id(id)) {
				if (config.debug > 1) printf("Serial - Invalid device id.\n");
				return 0;
			}

			dev = device_get(&bridge, id);
			if (!dev) {
				rc = device_load(&bridge, config.devices_folder, id);
				if (rc == -1) {
					run = 0;
					return sread;
				}
				if (rc) {
					rc = device_add_dev(&bridge, id, md_id);
					if (rc == -1) {
						run = 0;
						return sread;
					}
					if (rc) {
						if (config.debug > 2) printf("Serial - Failed to add device.\n");
						return sread;
					}
				}
				dev = device_get(&bridge, id);
				if (config.debug > 1) {
					printf("New device:\n");
					device_print_device(dev);
				}
			} else
				dev->alive = ALIVE_CNT;
			bridge_message(mosq, sd, dev, buf_p);
		} else {
			if (config.debug > 1) printf("Unknown serial data.\n");
		}
		return sread;
	}
	else if (buf_len == SERIAL_MAX_BUF) {
		if (config.debug > 1) printf("Serial buffer full.\n");
		buf_len = 0;
	} else {
		if (config.debug > 1) printf("Serial chunked.\n");
	}
	return 0;
}

void signal_usr(int sd, struct mosquitto *mosq)
{
	struct device *md_dev;
	struct module *md;
	char md_id[DEVICE_MD_ID_SIZE + 1];

	if (user_signal == MODULE_SIGUSR1) {
		if (config.remap_usr1)
			strcpy(md_id, config.remap_usr1);
		else
			strcpy(md_id, MODULE_SIGUSR1_ID);
	}
	else if (user_signal == MODULE_SIGUSR2) {
		if (config.remap_usr2)
			strcpy(md_id, config.remap_usr2);
		else
			strcpy(md_id, MODULE_SIGUSR2_ID);
	}

	md = device_get_module(&bridge, md_id);
	if (md) {
		md_dev = device_get(&bridge, md->device);
		if (!md_dev) {
			fprintf(stderr, "Error: Orphan module.\n");
			device_remove_module(&bridge, md_id);
			user_signal = 0;
			return;
		}
		if (md_dev->md_deps->type == MODULE_SERIAL && bridge.serial_ready) {
				snprintf(gbuf, GBUF_SIZE, "%s%s,%d,%s", SERIAL_INIT_MSG, md_dev->id, PROTO_MD_RAW, md->id);
				serialport_printlf(sd, gbuf);
		}

		if (connected)
			mqtt_publish(mosq, md->topic, "1");
	}
	user_signal = 0;
}

void serial_hang(struct mosquitto *mosq)
{
	struct module *md;

	bridge.serial_ready = false;
	bridge.serial_alive = 0;

	if (connected) {
		md = device_get_module(&bridge, MODULE_SERIAL_ID);
		if (md) {
			mqtt_publish(mosq, md->topic, "0");		// Serial is down message
		}
	}
}

void print_usage(char *prog_name)
{
	printf("Usage: %s [-c file] [--quiet]\n", prog_name);
	printf(" -c : config file path.\n");
}

int main(int argc, char *argv[])
{
	int sd = -1;
	char *conf_file = NULL;
	struct mosquitto *mosq;
	struct module *md;
	struct device *dev;
	int rc;
	int i;
	
	if (!quiet) printf("Version: %s\n", version);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
	signal(SIGUSR1, handle_signal);
	signal(SIGUSR2, handle_signal);
	signal(SIGALRM, each_sec);
	
	for (i=1; i<argc; i++) {
		if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")){
			if(i==argc-1){
                fprintf(stderr, "Error: -c argument given but no file specified.\n\n");
				print_usage(argv[0]);
                return 1;
            }else{
				conf_file = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "--quiet")){
				quiet = true;
		}else{
				fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
				print_usage(argv[0]);
				return 1;
		}
	}
	
	if(!conf_file) {
		fprintf(stderr, "Error: No config file given.\n");
		return 1;
	}

	memset(&config, 0, sizeof(struct bridge_config));
	if(config_parse(conf_file, &config)) return 1;
	
	if (quiet) config.debug = 0;
	if (config.debug != 0) printf("Debug: %d\n", config.debug);

	if (!device_isValid_id(config.id)) {
		fprintf(stderr, "Invalid id.\n");
		return -1;
	}
	if (device_init(&bridge, config.id) == -1)
		return 1;

	mosquitto_lib_init();
	mosq = mosquitto_new(config.id, true, NULL);
	if(!mosq){
		fprintf(stderr, "Error creating mqtt instance.\n");
		switch(errno){
			case ENOMEM:
				fprintf(stderr, " out of memory.\n");
				break;
			case EINVAL:
				fprintf(stderr, " invalid id.\n");
				break;
		}
		return 1;
	}
	snprintf(gbuf, GBUF_SIZE, "%d", PROTO_ST_TIMEOUT);
	mosquitto_will_set(mosq, bridge.status_topic, strlen(gbuf), gbuf, config.mqtt_qos, MQTT_RETAIN);
	mosquitto_connect_callback_set(mosq, on_mqtt_connect);
	mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
	mosquitto_message_callback_set(mosq, on_mqtt_message);
	mosquitto_user_data_set(mosq, &sd);

	if (config.debug > 1) printf("Subscribe topic: %s\n", bridge.config_topic);

	rc = device_add_module(&bridge, MODULE_MQTT_ID, bridge.id);				//TODO: autogen id?
	if (rc) {
		fprintf(stderr, "Failed to add mqtt module.\n");
		return 1;
	}

	if (config.scripts_folder) {
		if (access(config.scripts_folder, R_OK )) {
			fprintf(stderr, "Couldn't open scripts folder: %s\n", config.scripts_folder);
			return 1;
		}
		rc = device_add_module(&bridge, MODULE_SCRIPT_ID, bridge.id);		//TODO: autogen id?
		if (rc) {
			fprintf(stderr, "Failed to add script module.\n");
			return 1;
		}
	}
	if (config.interface) {
		//TODO: check if interface exists
		if (access("/proc/net/dev", R_OK )) {
			fprintf(stderr, "Couldn't open /proc/net/dev\n");
			return 1;
		}
		rc = device_add_module(&bridge, MODULE_BANDWIDTH_ID, bridge.id);		//TODO: autogen id?
		if (rc) {
			fprintf(stderr, "Failed to add bandwidth module.\n");
			return 1;
		}
		bandwidth = true;
	}
	if (config.serial.port) {
		sd = serialport_init(config.serial.port, config.serial.baudrate);
		if( sd == -1 ) {
			fprintf(stderr, "Couldn't open serial port.\n");
			return 1;
		} else {
			rc = device_add_module(&bridge, MODULE_SERIAL_ID, bridge.id);		//TODO: autogen id?
			if (rc) {
				fprintf(stderr, "Failed to add serial module.\n");
				return 1;
			}
			serialport_flush(sd);
			bridge.serial_ready = true;

			if (config.debug) printf("Serial ready.\n");
		}
	}

	rc = device_add_module(&bridge, MODULE_SIGUSR1_ID, bridge.id);			//TODO: autogen id?
	if (rc) {
		fprintf(stderr, "Failed to add sigusr1 module.\n");
		return 1;
	}

	rc = device_add_module(&bridge, MODULE_SIGUSR2_ID, bridge.id);			//TODO: autogen id?
	if (rc) {
		fprintf(stderr, "Failed to add sigusr2 module.\n");
		return 1;
	}

	device_print_modules(&bridge);

	rc = mosquitto_connect(mosq, config.mqtt_host, config.mqtt_port, 60);
	if (rc) {
		fprintf(stderr, "Wrong MQTT parameters. Check your config.\n");
		return -1;
	}

	alarm(1);

	while (run) {
		if (bridge.serial_ready) {
			rc = serial_in(sd, mosq, MODULE_SERIAL_ID);
			if (rc == -1) {
				serial_hang(mosq);
			} else if (rc > 0) {
				bridge.serial_alive = ALIVE_CNT;
			}
		}

		if (user_signal) {
			if (config.debug > 1) printf("Signal - SIGUSR: %d\n", user_signal);
			signal_usr(sd, mosq);
		}

		rc = mosquitto_loop(mosq, -1, 1);
		if (run && rc) {
			if (config.debug > 2) printf("MQTT loop: %s\n", mosquitto_strerror(rc));
			usleep(100000);	// wait 100 msec
			mosquitto_reconnect(mosq);
		}

		if (every30s) {
			every30s = false;

			bridge.controller = false;
			for (i = 0; i < bridge.devices_len; i++) {
				dev = &bridge.devices[i];
				if (dev->alive) {
					dev->alive--;
					if (!dev->alive) {
						snprintf(gbuf, GBUF_SIZE, "%d,%s", PROTO_ST_TIMEOUT, dev->id);
						mqtt_publish(mosq, bridge.status_topic, gbuf);

						if (dev->md_deps->type == MODULE_MQTT && dev->type == DEVICE_TYPE_NODE) {
							snprintf(gbuf, GBUF_SIZE, "status/%s", dev->id);
							rc = mosquitto_unsubscribe(mosq, NULL, gbuf);
							if (rc)
								fprintf(stderr, "Error: MQTT unsubscribe returned: %s\n", mosquitto_strerror(rc));
						}

						if (config.debug) printf("Device timeout - id: %s\n", dev->id);
					} else {
						if (dev->type == DEVICE_TYPE_CONTROLLER)
							bridge.controller = true;
					}
				}
			}

			if (!bridge.controller)
				bridge.modules_update = false;

			if (connected) {
				snprintf(gbuf, GBUF_SIZE, "%d,%d", PROTO_ST_ALIVE, bridge.modules_len);
				mqtt_publish(mosq, bridge.status_topic, gbuf);

				if (bridge.modules_update) {
					snprintf(gbuf, GBUF_SIZE, "%d", PROTO_ST_MODULES_UP);
					if (mqtt_publish(mosq, bridge.status_topic, gbuf))
						bridge.modules_update = false;
				}

				if (bandwidth) {
					md = device_get_module(&bridge, MODULE_BANDWIDTH_ID);
					if (md) {
						if (mqtt_publish_bandwidth(mosq, md->topic) == -1)
							break;
					}
				}
			} else {
				if (config.debug != 0) printf("MQTT Offline.\n");
			}

			if (bridge.serial_alive) {
				bridge.serial_alive--;
				if (!bridge.serial_alive) {
					if (config.debug > 1) printf("Serial timeout.\n");
					serial_hang(mosq);
				}
			} else {
				if (config.serial.port && !bridge.serial_ready) {
					if (config.debug > 1) printf("Trying to reconnect serial port.\n");
					serialport_close(sd);
					sd = serialport_init(config.serial.port, config.serial.baudrate);
					if( sd == -1 )
						fprintf(stderr, "Couldn't open serial port.\n");
					else {
						serialport_flush(sd);
						bridge.serial_ready = true;
						if (config.debug) printf("Serial reopened.\n");
					}
				}
			}
		}
		usleep(20);
	}

	if (bridge.serial_ready) {
		serialport_close(sd);
	}

	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();
	config_cleanup(&config);

	printf("Exiting..\n\n");

	return 0;
}
