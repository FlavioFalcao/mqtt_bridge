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

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

int getInt(char **buf, int *number)
{
	bool isInt = false;
	char ch;
	char *pt;
	int buf_len, i = 0;

	pt = *buf;
	*number = 0;

	if (pt == NULL) return 0;
	buf_len = strlen(pt);
	if (buf_len == 0) return 0;

	if ((char)pt[0] == GETINT_DLM)
		i = 1;
	for (; i < buf_len; i++) {
		ch = (char)pt[i];
		if (ch >= '0' && ch <= '9') {
			*number = (*number * 10) + (ch - '0');
			isInt = true;
		} else if (ch == GETINT_DLM) {
			i++;
			break;
		} else {
			*number = 0;
			return 0;
		}
	}
	*buf += i;		// Point to next position

	if (isInt)
		return i;
	else
		return 0;
}

int getString(char **buf, char *str, int size, char lim)
{
	char ch;
	char *pt;
	int buf_len;
	int cnt = 0, i = 0;

	pt = *buf;

	if (pt == NULL) return 0;
	buf_len = strlen(pt);
	if (buf_len == 0) return 0;

	if (pt[0] == lim)
		i = 1;

	for (; i < buf_len; i++) {
		ch = pt[i];

		if (ch == '\n') {
			i++;
			break;
		}
		if (ch == '\r') {
			continue;
		}
		if (ch == lim) {
			i++;
			break;
		}

		if (cnt == size) {
			break;
		}
		str[cnt++] = ch;
	}
	str[cnt] = 0;
	*buf += i;		// Point to next position

	return cnt;
}

int run_script(char *dir, char *scriptName, char *output, int output_max_size, int debug)
{
	FILE *pf;
	char *command;
	int command_len;
	int i;

	for (i = 0; i < strlen(scriptName); i++) {
		if (scriptName[i] == '.') {
			if (!strcmp(&scriptName[i], ".sh"))
				break;
			else {
				if (debug > 1) printf("Invalid script name.\n");
				return 1;
			}
		} else if((scriptName[i] >= 'a') && (scriptName[i] <= 'z')) {
			continue;
		} else if((scriptName[i] >= '0') && (scriptName[i] <= '9')) {
			continue;
		} else if(scriptName[i] == '-') {
			continue;
		} else if(scriptName[i] == '_') {
			continue;
		} else {
			if (debug > 1) printf("Invalid script name.\n");
			return 1;
		}
	}
	
	command_len = snprintf(NULL, 0, "%s/%s", dir, scriptName);
	if((command = malloc((command_len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		return -1;
	}
	snprintf(command, command_len + 1, "%s/%s", dir, scriptName);
	if (debug > 1) printf("script name: %s\n", scriptName);
	
	if( access( command, X_OK ) != -1 ) {
		pf = popen(command, "r");
		if (!pf) {
			fprintf(stderr, " Could not open pipe for output.\n");
			free(command);
			return 1;
		}
	 
		// Grab output from process execution
		fgets(output, output_max_size , pf);
	 
		if (pclose(pf) != 0) {
			fprintf(stderr," Error: Failed to close command stream \n");
			free(command);
			return 1;
		}

	} else {
		if (debug > 1) printf("Cannot execute: %s\n", command);
	}
	free(command);
	return 0;
}
