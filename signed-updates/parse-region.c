/*
 * build-region.c
 *
 * [Short description of file]
 *
 * Copyright 2007-2008 by Garmin Ltd. or its subsidiaries
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <getopt.h>

#define FILE_ID			0x7247704B
#define DATA_VERSION_TYPE	'D'
#define APP_VERSION_TYPE	'A'
#define REGION_TYPE		'R'

#define _stringize(s) #s
#define stringize(s) _stringize(s)

#define cond_print(fmt,...) do { \
	if (options.print) \
		printf (fmt, ##__VA_ARGS__); \
	} while (0)

/* Types as defined by "RGNPKG Document File Specification" */
typedef unsigned char BYTE;
typedef signed short SHORT;
typedef unsigned short USHORT;
typedef signed int INT;
typedef unsigned int UINT;
typedef char * STRING;
typedef BYTE * BSTRING;
typedef unsigned char BOOL;
typedef double DOUBLE;

/* Globals */
static int advr_count;
static int avr_count;
static int region_count;
static int app_record_count;
static int first_record_is_advr;
static int valid = 1;

/* Version identification record */
struct vir {
	UINT file_id;
	USHORT version;
} __attribute__ ((__packed__));

/* Data record */
struct data_record {
	UINT size;
	BYTE type;
} __attribute__ ((__packed__));

/* Application data version record */
struct advr {
	USHORT version;
} __attribute__ ((__packed__));

/* Application version record */
struct avr {
	USHORT version;
	STRING builder;
	STRING build_date;
	STRING build_time;
} __attribute__ ((__packed__));

/* Region record */
struct region {
	USHORT id;
	UINT delay;
	UINT size;
} __attribute__ ((__packed__));


struct options {
	int human_readable:1;
	int print:1;
	int extract;
} options;

/*
 * Return memory if available or exit with error.
 */
static void *
xmalloc (size_t size)
{
	void * mem;

	mem = malloc(size);
	if (!mem) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return mem;
}

/*
 * Read entire count from fd.  Exit on errors or EOF.
 */
static void
readall (int fd, void *buf, size_t count)
{
	size_t bytes_read;

	do {
		bytes_read = read(fd, buf, count);
		if (bytes_read < 0) {
			fprintf(stderr, "Error writing output file: %s\n",
					strerror(errno));
			exit(1);
		}
                if (bytes_read == 0) {
			fprintf(stderr, "Unexpected EOF\n");
			exit(1);
                }
		buf += bytes_read;
		count -= bytes_read;
	} while (count);
}


int
parse_vir (int fd)
{
	struct vir vir;

	readall (fd, &vir, sizeof(vir));

	cond_print ("Version Identification Record:\n");
	cond_print ("  File ID: 0x%08x (\"%c%c%c%c\")\n", vir.file_id,
				((char *)&vir.file_id)[3],
				((char *)&vir.file_id)[2],
				((char *)&vir.file_id)[1],
				((char *)&vir.file_id)[0]);
	cond_print ("  Version: %u.%02u\n", vir.version / 100, vir.version % 100);

	if (vir.file_id != FILE_ID) {
		cond_print ("Error:  VIR file ID is not correct.  Should be " stringize(FILE_ID) " (\"KpGr\")\n");
		valid = 0;
	}

	return 1;
}

void
parse_advr (int fd, UINT size)
{
	struct advr advr;

	readall (fd, &advr, size);

	cond_print ("Application Data Version Record:\n");
	cond_print ("  Version: %u.%02u\n", advr.version / 100, advr.version % 100);

	advr_count++;
	app_record_count++;

	if (app_record_count == 1)
		first_record_is_advr = 1;
	else
		first_record_is_advr = 0;
}

void
parse_avr (int fd, UINT size)
{
	struct avr avr;
	char *buf;
	int pos;

	/* Strings are of unknown length, so memory for
	 * this record must be dynamic */
	buf = xmalloc (size);
	readall (fd, buf, size);
	pos = 0;

	/* Copy version value into struct */
	avr.version = *(typeof(avr.version) *)&buf[pos];
	pos += sizeof (avr.version);

	/* Set string pointers in struct to locations in buf */
	avr.builder = &buf[pos];
	pos += strnlen (avr.builder, size - pos - 1) + 1;

	avr.build_date = &buf[pos];
	pos += strnlen (avr.build_date, size - pos - 1) + 1;

	avr.build_time = &buf[pos];
	pos += strnlen (avr.build_time, size - pos - 1) + 1;

	if (buf[size - 1] != 0) {
		buf[size - 1] = 0;
		valid = 0;
	}

	/* print it */
	cond_print ("Application Version Record:\n");
	cond_print ("  Version: %u.%02u\n", avr.version / 100, avr.version % 100);
	cond_print ("  Builder: %s\n", avr.builder);
	cond_print ("  Build date: %s\n", avr.build_date);
	cond_print ("  Build time: %s\n", avr.build_time);

	free (buf);

	avr_count++;
	app_record_count++;
}

void
print_human_readable (unsigned long long value)
{
	const char * units = "BKMGT";
	unsigned short last_k = 0;

	while (value >= 1024 && *units != 0) {
		last_k = value % 1024;
		value /= 1024;
		units++;
	}

	if (*units == 0)
		units--;

	if (value < 10 && *units != 'B') {
		int dec = (last_k * 10 + 512) / 1024;
		if (dec == 10) {
			dec = 0;
			value++;
		}
		printf ("%llu.%u", value, dec);
	}
	else
		printf ("%llu", last_k >= 512 ? value + 1 : value);
	printf("%c", *units);
}


void
fd_copy (int dst, int src, UINT size)
{
	char buf[PAGE_SIZE];

	while (size) {
		ssize_t bytes_read;
		size_t req = size > PAGE_SIZE ? PAGE_SIZE : size;
		char *pos;

		bytes_read = read (src, buf, req);
		if (bytes_read < 0) {
			fprintf (stderr, "Error reading from input\n");
			exit (1);
		}
		if (bytes_read == 0)
			return;
		size -= bytes_read;
		pos = buf;
		while (bytes_read) {
			int bytes_written;
			
			bytes_written = write (dst, pos, bytes_read);
			if (bytes_written < 0) {
				fprintf (stderr, "Error writing\n");
				exit (1);
			}
			bytes_read -= bytes_written;
			pos += bytes_written;
		}
	}
}


void
parse_region (int fd, UINT size)
{
	struct region region;

	readall (fd, &region, sizeof(region));

	region_count++;
	app_record_count++;

	if (options.print) {
		printf ("Region Record %d:\n", region_count);
		printf ("  ID: %d (0x%04x)\n", region.id, region.id);
		printf ("  Delay: %u\n", region.delay);
		printf ("  Size: ");
		if (options.human_readable)
			print_human_readable (region.size);
		else
			printf("%u", region.size);
		printf("\n");
	}

	if (options.extract == region_count) {
		fd_copy (1, fd, region.size);
		options.extract = -1;
	}
	else {
		lseek (fd, region.size, SEEK_CUR);
	}
}


void
init_options (struct options *opts)
{
	memset (opts, 0, sizeof(struct options));

	opts->extract = -1;
}


void
show_usage (void)
{
	printf("Usage: parse-region [OPTION]\n");
	printf("Process region-encoded data\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --human-readable  Print sizes in human-readable format\n");
	printf("      --help            Display this help message\n");
}


void
parse_args (int argc, char **argv, struct options *opts)
{
	int opt;
	void *tmp;

	enum {
		OPTION_HUMAN,
		OPTION_HELP,
		OPTION_PRINT,
		OPTION_EXTRACT,
	};

	struct option available_options[] = {
		{"human-readable",	no_argument,		NULL,	OPTION_HUMAN},
		{"help",		no_argument,		NULL,	OPTION_HELP},
		{"print",		no_argument,		NULL,	OPTION_PRINT},
		{"extract",		required_argument,	NULL,	OPTION_EXTRACT},
		{0, 0, 0, 0},
	};

	do {
		opt = getopt_long (argc, argv, "hpx:", available_options, NULL);

		switch (opt) {
			case 'h':
			case OPTION_HUMAN:
				opts->human_readable = 1;
				break;
			case OPTION_HELP:
				show_usage();
				exit (0);
				break;
			case 'p':
			case OPTION_PRINT:
				opts->print = 1;
				break;
			case 'x':
			case OPTION_EXTRACT:
				if (opts->extract >= 0) {
					fprintf (stderr, "Sorry, only one extraction at a time supported.\n");
					exit (1);
				}
				opts->extract = strtol (optarg, (char **)&tmp, 10);
				if (tmp == optarg) {
					fprintf (stderr, "Invalid region number\n");
					exit (1);
				}
				break;
			case -1:
				break;
			case '?':
			default:
				fprintf (stderr, "Try `parse-args --help' for more information\n");
				exit (1);
				break;
		}
	} while (opt >= 0);

	if (opts->extract >= 0 && opts->print) {
		fprintf (stderr, "Can't both print and extract to stdout\n");
		exit (1);
	}
}


int
parse_data_record (int fd)
{
	struct data_record dr;
	int bytes;

	bytes = read (fd, &dr, sizeof(dr));
	if (bytes == 0)
		return 0;

	switch (dr.type) {
		case DATA_VERSION_TYPE:
			parse_advr (fd, dr.size);
			break;
		case APP_VERSION_TYPE:
			parse_avr (fd, dr.size);
			break;
		case REGION_TYPE:
			parse_region (fd, dr.size);
			break;
		default:
			fprintf (stderr, "Unknown data record type: '%c'\n", dr.type);
			valid = 0;
			return 0;
			break;
	}

	return 1;
}


void
print_errors (void)
{
	if (advr_count < 1) {
		cond_print ("Error: RGN file must contain an Application Data Version Record\n");
		valid = 0;
	}
	if (advr_count > 1) {
		cond_print ("Error: More than one Application Data Version Record\n");
		valid = 0;
	}
	if (app_record_count > 0 && !first_record_is_advr) {
		cond_print ("Error: First application data record must be ADVR\n");
		valid = 0;
	}
	if (avr_count < 1) {
		cond_print ("Error: RGN file must contain an Application Version Record\n");
		valid = 0;
	}

	if (!valid)
		cond_print ("File is NOT valid\n");

	if (options.extract >= 0) {
		fprintf (stderr, "Region %d not found\n", options.extract);
		exit (1);
	}
}

int main(int argc, char **argv)
{
	int fd = STDIN_FILENO;
//	int fd = open("rgnpkg/a.rgn", O_RDONLY);

	init_options (&options);
	parse_args (argc, argv, &options);

	if (!parse_vir (fd))
		return 0;
	cond_print("\n");

	while (parse_data_record(fd)){
		cond_print("\n");
	}

	print_errors ();

//	close(fd);
	return 0;
}

