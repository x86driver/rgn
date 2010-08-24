/*
 * bin2c.c
 *
 * [Short description of file]
 *
 * Copyright 2007-2008 by Garmin Ltd. or its subsidiaries
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void usage(void)
{
	fprintf(stderr, "Formats binary data into an array for use in C.\n");
	fprintf(stderr, "Reads from stdin; writes to stdout.\n");

	exit(0);
}

int main(int argc, char **argv)
{
	int fd = 0, chars;
	unsigned char buf[10];

	if (argc > 1)
		usage();

	printf("static unsigned char data[] = {\n");
	do {
		int i;
		chars = read(fd, buf, 10);
		printf("\t");
		for (i=0; i<chars; i++)
			printf("0x%02x, ", buf[i]);
		printf("\n");
	} while (chars == 10);
	printf("};\n");

	return 0;
}
	
