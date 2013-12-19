/*
* Original work from dwm - dynamic window manager, http://dwm.suckless.org/
*/

int parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs, char *_dev)
{
	char *buf, *devstart;
	static int bufsize;
	const int size = 20;
	char dev[size];
	FILE *devfd;

	buf = (char *) calloc(255, 1);
	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");

	// ignore the first two lines of the file
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
		snprintf(dev, size, "%s:", _dev);
	    if ((devstart = strstr(buf, dev)) != NULL) {
			// With thanks to the conky project at http://conky.sourceforge.net/
			sscanf(devstart + strlen(_dev) + 2, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
		       receivedabs, sentabs);
			fclose(devfd);
			free(buf);
			return 0;
	    }
	}
	fclose(devfd);
	free(buf);
	return 1;
}
