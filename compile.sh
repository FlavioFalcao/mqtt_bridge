#!/bin/bash
rm -rf mqtt_bridge
gcc -Wall -lmosquitto mqtt_bridge.c utils.c conf.c device.c arduino-serial-lib.c -o mqtt_bridge
