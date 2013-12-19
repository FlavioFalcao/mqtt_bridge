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

#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#define ALIVE_CNT 3
#define TOPIC_MIN_SIZE 3
#define TOPIC_MAX_SIZE 30

#define MQTT_RETAIN 0

#define PROTO_ERROR 0
#define PROTO_ACK 1
#define PROTO_NACK 2
#define PROTO_ST_ALIVE 3				// "status/id"
#define PROTO_ST_TIMEOUT 4				// "status/id"
#define PROTO_ST_MODULES_UP 5			// "status/id"
#define PROTO_MODULE 6
#define PROTO_GET_MODULE 7
#define PROTO_GET_MODULES 8
#define PROTO_MD_TOPIC 9
#define PROTO_MD_GET_TOPIC 10
#define PROTO_MD_SET_TOPIC 11
#define PROTO_MD_RAW 12					// Module topic
#define PROTO_MD_TO_RAW 13
#define PROTO_MD_ENABLE 14
#define PROTO_MD_GET_ENABLE 15
#define PROTO_MD_SET_ENABLE 16
#define PROTO_MD_SET_ID 17
#define PROTO_DEVICE 18
#define PROTO_GET_DEVICES 19
#define PROTO_SAVE_DEVICE 20
#define PROTO_REMOVE_DEVICE 21

struct bridge_serial{
	char *port;
	int baudrate;
	int timeout;
	int qos;
};

struct bridge_config{
	int debug;
	char *id;
	char *mqtt_host;
	int mqtt_port;
	int mqtt_qos;
	struct bridge_serial serial;
	char *devices_folder;
	char *scripts_folder;
	char *interface;
	char *remap_usr1;
	char *remap_usr2;
};

int config_parse(const char *conffile, struct bridge_config *config);
void config_cleanup(struct bridge_config *config);

#endif
