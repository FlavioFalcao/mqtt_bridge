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

#include "device.h"
#include "mqtt_bridge.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char *modules_name[MODULES_NAME_SIZE] = {"dummy", "temp", "ldr", "hum", "zmon", "acpower", "dcpower", "amps", "volts" 
	, "watts", "rain", "sonar", "led", "rgb", "lcd16x2", "bts", "btl", "flag1", "flag2", "flag3", "flag4", "flag5", "script"
	, "bandwidth", "serial", "mqtt", "sigusr1", "sigusr2"};

int device_init(struct bridge *bdev, char *id)
{
	int topic_len;

	if (!device_isValid_id(id))
		return 1;

	bdev->id = id;
	bdev->controller = false;
	bdev->serial_ready = 0;
	bdev->serial_alive = 0;
	bdev->modules_len = 0;
	bdev->module = NULL;
	bdev->devices_len = 0;
	bdev->devices = NULL;
	bdev->modules_update = false;

	topic_len = snprintf(NULL, 0, "config/%s", id);
	if((bdev->config_topic = (char *)malloc((topic_len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(bdev->config_topic, topic_len + 1, "config/%s", id);

	topic_len = snprintf(NULL, 0, "status/%s", id);
	if((bdev->status_topic = (char *)malloc((topic_len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(bdev->status_topic, topic_len + 1, "status/%s", id);

	return 0;
}

int device_add_module(struct bridge *bdev, char *md_id, char *dev_id)
{
	struct module *md;

	if (!device_isValid_md_id(md_id))
		return 1;
	if (!device_isValid_id(dev_id))
		return 1;

	for (md = bdev->module; md != NULL; md = md->next) {
		if (!strcmp(md->id, md_id))
			return 1;
	}

	if ((md = malloc(sizeof(struct module))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}

	bdev->modules_len++;
	md->next = bdev->module;
	bdev->module = md;

	md->id = strdup(md_id);
	if (!md->id) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}
	md->device = strdup(dev_id);
	if (!md->device) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}
	md->enabled = true;
	md->type = (((md_id[0] - 48) * 100) + ((md_id[1] - 48) * 10) + (md_id[2] - 48));
	md->topic = NULL;
	bdev->modules_update = true;

	return device_set_md_default_topic(md, bdev->id);
}

int device_set_md_default_topic(struct module *module, char *id)
{
	int topic_len;

	if (!device_isValid_id(id))
		return 1;

	if (module->topic) {
		free(module->topic);
	}

	topic_len = snprintf(NULL, 0, "raw/%s/%s", id, module->id);
	if ((module->topic = malloc((topic_len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(module->topic, topic_len + 1, "raw/%s/%s", id, module->id);

	return 0;
}

int device_set_md_topic(struct module *module, char *topic)
{
	if (strlen(topic) < TOPIC_MIN_SIZE || strlen(topic) > TOPIC_MAX_SIZE)
		return 1;

	if (!strcmp(topic, module->topic))
		return 1;

	if (module->topic) {
		free(module->topic);
	}

	module->topic = strdup(topic);
	if (!module->topic) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}

	return 0;
}

struct module* device_get_module(struct bridge *bdev, char *md_id)
{
	struct module *md;

	if (!device_isValid_md_id(md_id))
		return NULL;

	for (md = bdev->module; md != NULL; md = md->next) {
		if (!strcmp(md->id, md_id))
				return md;
	}
	return NULL;
}

int device_remove_module(struct bridge *bdev, char *md_id)
{
	struct module *prev_md, *md;

	if (!device_isValid_md_id(md_id))
		return 1;

	for (prev_md = md = bdev->module; md != NULL; md = md->next) {
		if (strcmp(md->id, md_id)) {
			prev_md = md;
			continue;
		}

		prev_md->next = md->next;
		bdev->modules_len--;
		free(md->id);
		free(md->device);
		free(md->topic);
		free(md);
		return 0;
	}
	return 1;
}

void device_print_module(struct module *md)
{
	printf("       id: %s\n       type: %s\n       enabled: %d\n       device: %s\n       topic: %s\n",
			md->id, modules_name[md->type], md->enabled, md->device, md->topic);
}

void device_print_modules(struct bridge *bdev)
{
	struct module *md;

	printf("Modules:\n");
	
	for (md = bdev->module; md != NULL; md = md->next) {
		device_print_module(md);
	}
}

int device_add_dev(struct bridge *bdev, char *id, char *md_id)
{
	struct device *current;
	struct module *md;
	int i;

	if (!device_isValid_id(id))
		return 1;

	if (!device_isValid_md_id(md_id))
		return 1;
	md = device_get_module(bdev, md_id);
	if (!md)
		return 1;

	for (i = 0; i < bdev->devices_len; i++) {
		if (!strcmp(bdev->devices[i].id, id))
			return 1;
	}

	bdev->devices_len++;
	bdev->devices = realloc(bdev->devices, sizeof(struct device) * bdev->devices_len);
	if (!bdev->devices) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}

	current = &bdev->devices[bdev->devices_len - 1];
	current->id = strdup(id);
	if (!current->id) {
		fprintf(stderr, "Error: No memory left.\n");
		return -1;
	}
	current->md_deps = md;
	current->alive = ALIVE_CNT;
	current->type = id[0] - 48;
	current->modules = 0;

	if (!strcmp(md_id, MODULE_MQTT_ID)) {
		i = snprintf(NULL, 0, "config/%s", id);
		if((current->topic = malloc((i + 1)* (sizeof(char)))) == NULL) {
			fprintf(stderr, "No memory left.\n");
			return -1;
		}
		snprintf(current->topic, i + 1, "config/%s", id);
	} else {
		current->topic = NULL;
	}

	return 0;
}

int device_remove_dev(struct bridge *bdev, char *id)
{
	struct device *new_devices, *current;
	int i;

	if (!device_isValid_id(id))
		return 1;

	for (i = 0; i < bdev->devices_len; i++) {
		current = &bdev->devices[i];
		if (strcmp(current->id, id))
			continue;

		if ((new_devices = malloc((bdev->devices_len - 1) * sizeof(struct device))) == NULL) {
			fprintf(stderr, "No memory left.\n");
			return -1;
		}
		if (i != 0)
			memcpy(new_devices, bdev->devices, i * sizeof(struct device));
		if (i != bdev->devices_len - 1)
			memcpy(new_devices + i, bdev->devices + i + 1, (bdev->devices_len - i - 1) * sizeof(struct device));

		free(current->id);
		free(current->md_deps);
		if (current->topic)
			free(current->topic);

		free(bdev->devices);
		bdev->devices = new_devices;
		bdev->devices_len--;

		return 0;
	}
	return 1;
}

struct device *device_get(struct bridge *bdev, char *id)
{
	int i;

	if (!device_isValid_id(id))
		return NULL;

	for (i = 0; i < bdev->devices_len; i++) {
		if (!strcmp(bdev->devices[i].id, id))
			return &bdev->devices[i];
	}
	return NULL;
}

struct device *device_get_by_deps(struct bridge *bdev, char *md_deps)
{
	int i;

	if (!device_isValid_md_id(md_deps))
		return NULL;

	for (i = 0; i < bdev->devices_len; i++) {	
		if (!strcmp(bdev->devices[i].md_deps->id, md_deps))
			return &bdev->devices[i];
	}
	return NULL;
}

int device_isValid_id(char *id)
{
	int type;

	type = id[0] - 48;
	if (type < 0 || type > DEVICE_MAX_TYPE)
		return 0;

	if (strlen(id) != DEVICE_ID_SIZE)
		return 0;

	return 1;
}

int device_isValid_md_id(char *md_id)
{
	int type;

	if (strlen(md_id) != DEVICE_MD_ID_SIZE)
		return 0;

	type = (((md_id[0] - 48) * 100) + ((md_id[1] - 48) * 10) + (md_id[2] - 48));
	if (type < 0 || type > 255 || type >= MODULES_NAME_SIZE)
		return 0;

	return 1;
}

void device_print_device(struct device *dev)
{
	printf("       id: %s\n       type: %d\n       alive: %d\n       depends: %s\n       modules: %d\n       topic: %s\n",
	dev->id, dev->type, dev->alive, dev->md_deps->id, dev->modules, dev->topic);
}

void device_print_devices(struct bridge *bdev)
{
	struct device *current;
	int i;

	printf("Devices:\n");
	for (i = 0; i < bdev->devices_len; i++) {
		current = &bdev->devices[i];
		device_print_device(current);
	}
}

int device_save(struct bridge *bdev, char *folder, struct device *dev)
{
	FILE *fptr;
	char *dev_file;
	struct module *md;
	const int line_size = 100;
	char line[line_size + 1];
	int len;

	len = strlen(folder) + DEVICE_ID_SIZE + 2;
	if((dev_file = (char *)malloc((len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(dev_file, len + 1, "%s/%s", folder, dev->id);

	fptr = fopen(dev_file, "w");
	if(!fptr){
		fprintf(stderr, "Error opening device file for write \"%s\".\n", dev_file);
		free(dev_file);
		return 1;
	}

	snprintf(line, line_size, "device,%s,%s\n", dev->id, dev->md_deps->id);
	fputs(line, fptr);
	for (md = bdev->module; md != NULL; md = md->next) {
		if (!strcmp(md->device, dev->id)) {
			snprintf(line, line_size, "module,%s,%s,%d\n", md->id, md->topic, md->enabled);
			fputs(line, fptr);
		}
	}
	fclose(fptr);

	free(dev_file);
	return 0;
}

int device_load(struct bridge *bdev, char *folder, char *dev_id)
{
	FILE *fptr;
	char buf[1024];
	char *bufptr;
	char new_devId[DEVICE_ID_SIZE + 1];
	char md_id[DEVICE_MD_ID_SIZE + 1];
	char topic[TOPIC_MAX_SIZE + 1];
	struct module *md;
	int enabled;
	char *dev_file;
	int len, return_val = 0;

	len = strlen(folder) + DEVICE_ID_SIZE + 2;
	if((dev_file = (char *)malloc((len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(dev_file, len + 1, "%s/%s", folder, dev_id);

	fptr = fopen(dev_file, "rt");
	if(!fptr){
		free(dev_file);
		return 1;
	}

	while (fgets(buf, 1024, fptr)) {
		if (buf[0] != '#' && buf[0] != 10 && buf[0] != 13) {
			while (buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13) {
				buf[strlen(buf)-1] = 0;
			}
			if (!strncmp(buf, "device,", 7)) {
				bufptr = &buf[7];
				if (getString(&bufptr, new_devId, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (strcmp(dev_id, new_devId)) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (getString(&bufptr, md_id, DEVICE_MD_ID_SIZE, ',') != DEVICE_MD_ID_SIZE) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (device_add_dev(bdev, dev_id, md_id) == -1) {
					return_val = -1;
					break;
				}
			}
			else if (!strncmp(buf, "module,", 7)) {
				bufptr = &buf[7];
				if (getString(&bufptr, md_id, DEVICE_MD_ID_SIZE, ',') != DEVICE_MD_ID_SIZE) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (getString(&bufptr, topic, TOPIC_MAX_SIZE, ',') < TOPIC_MIN_SIZE) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (!getInt(&bufptr, &enabled)) {
					fprintf(stderr, "Invalid device file: %s\n", dev_id);
					return_val = 1;
					break;
				}
				if (device_add_module(bdev, md_id, dev_id) == -1) {
					return_val = -1;
					break;
				}
				md = device_get_module(bdev, md_id);
				if (!enabled)
					md->enabled = 0;
				if (strcmp(topic, md->topic)) {
					if (device_set_md_topic(md, topic) == -1) {
						return_val = -1;
						break;
					}
				}
			}
		}
	}
	fclose(fptr);

	free(dev_file);
	return return_val;
}
