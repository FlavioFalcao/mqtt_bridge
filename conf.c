/*
Original work Copyright (c) 2012 Roger Light <roger@atchoo.org>
Modified work Copyright (c) 2013 Marcelo Aquino, https://github.com/mapnull
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the project nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mqtt_bridge.h"
#include "device.h"

static int _conf_parse_int(char *token, const char *name, int *value);
static int _conf_parse_string(char *token, const char *name, char **value);

int config_parse(const char *config_file, struct bridge_config *config)
{
	FILE *fptr;
	char buf[1024];
	struct bridge_serial *current_serial = NULL;

	fptr = fopen(config_file, "rt");
	if(!fptr){
		fprintf(stderr, "Error opening config file \"%s\".\n", config_file);
		return 1;
	}

	config->debug = 0;
	config->id = NULL;
	config->mqtt_host = NULL;
	config->mqtt_port = 1883;
	config->mqtt_qos = 0;
	config->serial.port = NULL;
	config->devices_folder = NULL;
	config->scripts_folder = NULL;
	config->interface = NULL;
	config->remap_usr1 = NULL;
	config->remap_usr2 = NULL;

	while (fgets(buf, 1024, fptr)) {
		if (buf[0] != '#' && buf[0] != 10 && buf[0] != 13) {
			while (buf[strlen(buf)-1] == 10 || buf[strlen(buf)-1] == 13) {
				buf[strlen(buf)-1] = 0;
			}
			if (!strncmp(buf, "debug ", 6)) {
				if (_conf_parse_int(&(buf[6]), "debug", &config->debug)) {
					fclose(fptr);
					return 1;
				} else {
					if (config->debug < 0 || config->debug > 4) {
						fprintf(stderr, "Error: debug out of range in config.\n");
						fclose(fptr);
						return 1;
					}
				}
			} else if (!strncmp(buf, "id ", 3)) {
				if (_conf_parse_string(&(buf[3]), "id", &config->id)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "mqtt_host ", 10)) {
				if (_conf_parse_string(&(buf[10]), "mqtt_host", &config->mqtt_host)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "mqtt_port ", 10)){
				if (_conf_parse_int(&(buf[10]), "mqtt_port", &config->mqtt_port)) {
					fclose(fptr);
					return 1;
				} else {
					if (config->mqtt_port < 1 || config->mqtt_port > 65535) {
						fprintf(stderr, "Error: Invalid port given: %d\n", config->mqtt_port);
						fclose(fptr);
						return 1;
					}
				}
			} else if (!strncmp(buf, "mqtt_qos ", 9)) {
				if (_conf_parse_int(&(buf[9]), "mqtt_qos", &config->mqtt_qos)) {
					fclose(fptr);
					return 1;
				} else {
					if (config->mqtt_qos < 0 || config->mqtt_qos > 2) {
						fprintf(stderr, "Error: mqtt_qos out of range in config.\n");
						fclose(fptr);
						return 1;
					}
				}
			} else if (!strncmp(buf, "devices_folder ", 15)) {
				if (_conf_parse_string(&(buf[15]), "devices_folder", &config->devices_folder)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "scripts_folder ", 15)) {
				if (_conf_parse_string(&(buf[15]), "scripts_folder", &config->scripts_folder)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "port ", 5)) {
				current_serial = &config->serial;
				current_serial->baudrate = 9600;
				current_serial->timeout = 100;
				current_serial->qos = 0;

				if (_conf_parse_string(&(buf[5]), "port", &current_serial->port)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "baudrate ", 9)) {
				if (current_serial) {
					if (_conf_parse_int(&(buf[9]), "baudrate", &current_serial->baudrate)) {
						fclose(fptr);
						return 1;
					}
					if (current_serial->baudrate != 4800 && current_serial->baudrate != 9600
					&& current_serial->baudrate != 14400 && current_serial->baudrate != 19200
					&& current_serial->baudrate != 28800 && current_serial->baudrate != 38400
					&& current_serial->baudrate != 57600 && current_serial->baudrate != 115200) {
						fprintf(stderr, "Error: invalid baudrate.\n");
						fclose(fptr);
						return 1;
					}
				} else {
					fprintf(stderr, "Error: baudrate keyword without serial_port in config.\n");
					fclose(fptr);
					return 1;
				}
			} else if(!strncmp(buf, "timeout ", 8)) {
				if (current_serial){
					if (_conf_parse_int(&(buf[8]), "timeout", &current_serial->timeout)) {
						fclose(fptr);
						return 1;
					}
				} else {
					fprintf(stderr, "Error: timeout keyword without serial_port in config.\n");
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "interface ", 10)) {
				if (_conf_parse_string(&(buf[10]), "interface", &config->interface)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "remap_usr1 ", 11)) {
				if (_conf_parse_string(&(buf[11]), "remap_usr1", &config->remap_usr1)) {
					fclose(fptr);
					return 1;
				}
			} else if (!strncmp(buf, "remap_usr2 ", 11)) {
				if (_conf_parse_string(&(buf[11]), "remap_usr2", &config->remap_usr2)) {
					fclose(fptr);
					return 1;
				}
			} else {
				fprintf(stderr, "Warning: Unknown config option \"%s\".\n", buf);
			}
		}
	}
	fclose(fptr);

	if (!config->id) {
		fprintf(stderr, "Error: No id found in config file.\n");
		return 1;
	}
	if (strlen(config->id) != DEVICE_ID_SIZE) {
		fprintf(stderr, "Error: Invalid id.\n");
		return 1;
	}
	if (!config->mqtt_host) {
		config->mqtt_host = strdup("localhost");
	}

	return 0;
}


void config_cleanup(struct bridge_config *config)
{
	free(config->id);
	free(config->mqtt_host);
	if(config->serial.port != NULL)
		free(config->serial.port);
	if (config->devices_folder != NULL)
		free(config->devices_folder);
	if (config->scripts_folder != NULL)
		free(config->scripts_folder);
	if (config->interface != NULL)
		free(config->interface);
	if (config->remap_usr1 != NULL)
		free(config->remap_usr1);
	if (config->remap_usr2 != NULL)
		free(config->remap_usr2);
}


static int _conf_parse_int(char *token, const char *name, int *value)
{
	if (token){
		*value = atoi(token);
	} else {
		fprintf(stderr, "Error: Empty %s value in configuration.\n", name);
		return 1;
	}

	return 0;
}

static int _conf_parse_string(char *token, const char *name, char **value)
{
	if (token) {
		if (*value) {
			fprintf(stderr, "Error: Duplicate %s value in configuration.\n", name);
			return 1;
		}
		while (token[0] == ' ' || token[0] == '\t')
			token++;
		*value = strdup(token);
		if (!*value) {
			fprintf(stderr, "Error: Out of memory.\n");
			return 1;
		}
	} else {
		fprintf(stderr, "Error: Empty %s value in configuration.\n", name);
		return 1;
	}
	return 0;
}
