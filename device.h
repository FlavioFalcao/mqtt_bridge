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

#ifndef DEVICE_H
#define DEVICE_H

#include <stdbool.h>

#define DEVICE_VERSION "1.01"

#define DEVICE_ID_SIZE 9
#define DEVICE_MD_ID_SIZE 7

#define DEVICE_TYPE_NODE 0
#define DEVICE_TYPE_BRIDGE 1
#define DEVICE_TYPE_CONTROLLER 2
#define DEVICE_MAX_TYPE 2

#define MODULE_DUMMY 0					// Dummy module
#define MODULE_TEMP 1					// Temperature Celsius
#define MODULE_LDR 2					// Light sense
#define MODULE_HUM 3					// Humidity
#define MODULE_ZMON 4					// Zone monitoring
#define MODULE_AC 5						// AC Power
#define MODULE_DC 6						// DC Power
#define MODULE_AMP 7					// Amperemeter
#define MODULE_VOLT 8					// Voltmeter
#define MODULE_WATT 9					// Wattmeter
#define MODULE_RAIN 10					// Rain sensor
#define MODULE_SONAR 11					// Sonar
#define MODULE_LED 12					// Led
#define MODULE_LEDRGB 13				// RGB Led
#define MODULE_LCD16X2 14				// LCD 16X2
#define MODULE_BT_SHORT 15				// Button short press
#define MODULE_BT_LONG 16				// Button long press
#define MODULE_FLAG1 17					// Custom Flag 1
#define MODULE_FLAG2 18					// Custom Flag 2
#define MODULE_FLAG3 19					// Custom Flag 3
#define MODULE_FLAG4 20					// Custom Flag 4
#define MODULE_FLAG5 21					// Custom Flag 5
#define MODULE_SCRIPT 22
#define MODULE_BANDWIDTH 23
#define MODULE_SERIAL 24
#define MODULE_MQTT 25
#define MODULE_SIGUSR1 26
#define MODULE_SIGUSR2 27

#define MODULES_NAME_SIZE 28

#define MODULE_SCRIPT_ID "022FFA1"
#define MODULE_BANDWIDTH_ID "023FFA1"
#define MODULE_SERIAL_ID "024FFA1"
#define MODULE_MQTT_ID "025FFA1"
#define MODULE_SIGUSR1_ID "026FFA1"
#define MODULE_SIGUSR2_ID "027FFA1"

struct bridge {
	char *id;
	bool controller;
	bool serial_ready;
	int serial_alive;
	int modules_len;
	struct module *module;
	int devices_len;
	struct device *devices;
	bool modules_update;
	char *config_topic;
	char *status_topic;
};

struct device {
	char *id;
	int type;
	int alive;
	struct module *md_deps;
	int modules;
	char *topic;
};

struct module {
	char *id;
	int type;
	bool enabled;
	char *device;
	char *topic;
	struct module *next;
};

int device_init(struct bridge *, char *);
int device_add_module(struct bridge *, char *, char *);
int device_set_md_default_topic(struct module *, char *);
int device_set_md_topic(struct module *, char *);
struct module *device_get_module(struct bridge *, char *);
int device_remove_module(struct bridge *, char *);
void device_print_module(struct module *);
void device_print_modules(struct bridge *);
int device_add_dev(struct bridge *, char *, char *);
struct device *device_get(struct bridge *, char *);
struct device *device_get_by_deps(struct bridge *, char *);
int device_isValid_id(char *);
int device_isValid_md_id(char *);
void device_print_device(struct device *);
void device_print_devices(struct bridge *);
int device_save(struct bridge *, char *, struct device *);
int device_load(struct bridge *, char *, char *);

#endif