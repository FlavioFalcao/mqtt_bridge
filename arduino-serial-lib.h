/*
arduino-serial-lib -- simple library for reading/writing serial ports

Original work Copyleft (c) 2006-2013, Tod E. Kurt, http://todbot.com/blog/
https://github.com/todbot/arduino-serial

Modified work Copyleft (c) Marcelo Aquino, https://github.com/mapnull

*/

#ifndef __ARDUINO_SERIAL_LIB_H__
#define __ARDUINO_SERIAL_LIB_H__

#include <stdint.h>   // Standard types

int serialport_init(const char* serialport, int baud);
int serialport_close(int fd);
int serialport_writebyte( int fd, uint8_t b);
int serialport_write(int fd, const char* str);
int serialport_printlf(int fd, const char* str);
int serialport_printbytelf(int fd, uint8_t b);
int serialport_read_until(int fd, char* buf, char until, int buf_max,int timeout);
int serialport_flush(int fd);

#endif
