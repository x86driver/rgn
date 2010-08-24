/*
 * build-region.c
 *
 * [Short description of file]
 *
 * Copyright 2007-2008 by Garmin Ltd. or its subsidiaries
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

/* Region file header information */
#define FILE_ID			0x7247704B
#define DATA_VERSION_REC_CHAR	'D'
#define APP_VERSION_REC_CHAR	'A'
#define REGION_REC_CHAR		'R'

#define PRODUCT_VERSION_MAJOR	(2)
#define PRODUCT_VERSION_MINOR	(0)
#define LOW_LEVEL_VERSION	100
#define DATA_VERSION		100
#define APP_VERSION 		((PRODUCT_VERSION_MAJOR * 100) \
					+ PRODUCT_VERSION_MINOR )
/* Build stamps can be defined in a makefile.  Otherwise use these defaults. */
#ifndef BUILD_DATE
#define BUILD_DATE "Mon dd yyyy" 
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "hh:mm:ss"
#endif
#ifndef BUILD_UID
#define BUILD_UID "SQEQA"
#endif

/* Memory parameters */
#define FILE_BUF_SIZE (4096)
#define REGION_ALLOC_NUM 10
#define RECORD_BUFFER_SIZE 256
#define ERROR_SIZE (70)

/* Structure for holding region information */
struct region {
	char * file;
	unsigned int delay;
	unsigned int size;
	unsigned short id;
};

/* Format of region data in region header */
struct region_header {
	unsigned short id;
	unsigned int delay;
	unsigned int size;
} __attribute__ ((__packed__));

/* Macro to build a buffer of null-terminated strings */
#define ADD_STRING(buf, str, max, size) \
	do {						\
		strncpy(&buf[size], str, max-size);	\
		size += strlen(str) + 1;		\
	} while (0)


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
 * Return reallocated memory if available or exit with error.
 */
static void *
xrealloc (void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (!ptr) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	return ptr;
}

/*
 * Print program usage information and exit.
 */
static void
usage_and_quit(void)
{
	printf("Usage: build_region [OPTION] "
			"<input_file>,<region_id>,<delay_ms>...\n");
	printf("Build a region file for use with Garmin updater.exe\n");
	printf("\n");
	printf("  -o FILE      Specify a file to write to (default stdout)\n");
	printf("  -h, --help   Display this help message\n");
	printf("\n");
	printf("  input_file - File containing binary region data\n");
	printf("  region_id - Enumerated region type\n");
	printf("  delay_ms - Delay after applying this region\n");
	printf("\n");
	printf("Example:\n");
	printf("  build_region -o foo.rgn region1.bin,25,3000 "
							"23,region2.bin,0\n");
	fflush(stdout);
	exit(0);
}

static void
argument_error(const char *error)
{
	fprintf(stderr, error);
	fprintf(stderr, "\nTry `build_region --help` for more information.\n");
	exit(1);
}

/*
 * Write entire buffer to fd.  Exit if write errors occur.
 */
static void
writeall (int fd, const void *buf, size_t count)
{
	size_t bytes_written;

	do {
		bytes_written = write(fd, buf, count);
		if (bytes_written < 0) {
			fprintf(stderr, "Error writing output file: %s\n",
					strerror(errno));
			exit(1);
		}
		buf += bytes_written;
		count -= bytes_written;
	} while (count);
}

/*
 * Write a region file header to open file descriptor fd.
 */
static void
write_header (int fd, unsigned int fid, unsigned short ll_version)
{
	writeall(fd, &fid, sizeof(fid));
	writeall(fd, &ll_version, sizeof(ll_version));
}

/*
 * Write a region file record to open file descriptor fd.
 *
 * A record is: (uint)  Record size
 *              (char)  Record type
 *                      Record data
 */
static void
write_record (int fd, unsigned int size, char type, const void *buf,
							unsigned int buf_len)
{
	writeall(fd, &size, sizeof(size));
	writeall(fd, &type, sizeof(type));
	writeall(fd, buf, buf_len);
}

/*
 * Region files are given on the command line as triplets in the form:
 *     input_file,region_id,delay_ms
 * This function parses this string and stores the data in the struct region
 * array pointed to by regions.
 */
static void
parse_region_triplet(	const char *triplet,
			struct region ***regions,
			int *region_count)
{
	struct region *region;
	const char *pos;
	char *tmp;
	char error[ERROR_SIZE];

	/* Expand list if necessary */
	if (*region_count % REGION_ALLOC_NUM == 0) {
                size_t new_size = (*region_count + REGION_ALLOC_NUM) *
                                                sizeof(struct region *);
		*regions = xrealloc(*regions, new_size);
	}

	/* Get space for new region */
	region = xmalloc( sizeof(struct region) );

	/* Read file name */
	pos = triplet;
	while (*pos != ',') {
		if (*pos == 0) {
			snprintf(error, ERROR_SIZE, "Invalid region "
					"specification: %s", triplet);
			argument_error(error);
		}
		pos++;
	}
	region->file = xmalloc(pos - triplet + 1);
	memcpy(region->file, triplet, pos - triplet);
        region->file[pos - triplet] = 0;
	pos++;

	/* Read region id */
	region->id = strtol(pos, &tmp, 10);
	pos = tmp;
	if (*pos != ',') {
		snprintf(error, ERROR_SIZE, "Invalid region specification: %s",
				triplet);
		argument_error(error);
	}
	pos++;
	
	/* Read delay */
	region->delay = strtol(pos, NULL, 10);

	/* Add region */
	(*regions)[*region_count] = region;
	*region_count += 1;
}	

/*
 * Parse command line arguments and create output file.
 */
int main(int argc, char **argv)
{
	struct region **regions = NULL;
	int region_count = 0;
	const char *out_file_name = NULL;
	char buf[RECORD_BUFFER_SIZE];
	int i, out_fd, buf_size;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			char error[ERROR_SIZE];
			switch (argv[i][1]) {
			case '-':
				if (!strcmp(argv[i]+2, "help"))
					usage_and_quit();
				snprintf(error, ERROR_SIZE, "Unrecognized "
						"option: %s", argv[i]+2);
				argument_error(error);
				break;
			case 'o':
				i++;
				out_file_name = argv[i];
				break;
			case 'h':
				usage_and_quit();
				break;
			default:
				snprintf(error, ERROR_SIZE, "Unrecognized "
						"option: %c", argv[i][1]);
				argument_error( error );
				break;
			}
		}
		else {
			/* All non-switch options are region file triplets */
			parse_region_triplet(argv[i], &regions, &region_count);
		}
	}

	if (out_file_name) {
		out_fd = open(out_file_name, O_WRONLY | O_CREAT | O_TRUNC, 
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (out_fd < 0) {
			fprintf(stderr, "Could not open %s: %s\n",
					out_file_name, strerror(errno));
			exit(1);
		}
	}
	else {
		out_fd = STDOUT_FILENO;
	}

	/* Write file header */
	write_header(out_fd, FILE_ID, LOW_LEVEL_VERSION);

	/* Write data version record */
	buf[0] = DATA_VERSION;
	buf[1] = 0;
	buf_size = 2;
	write_record(out_fd, buf_size, DATA_VERSION_REC_CHAR, buf, buf_size);

	/* Write application version record */
	buf[0] = APP_VERSION;
	buf[1] = 0;
	buf_size = 2;
	ADD_STRING(buf, BUILD_UID, RECORD_BUFFER_SIZE, buf_size);
	ADD_STRING(buf, BUILD_DATE, RECORD_BUFFER_SIZE, buf_size);
	ADD_STRING(buf, BUILD_TIME, RECORD_BUFFER_SIZE, buf_size);
	write_record(out_fd, buf_size, APP_VERSION_REC_CHAR, buf, buf_size);

	/* Write a region record for each region.
	 * The body of the record contains the region header and the region.
	 */
	for (i = 0; i < region_count; i++) {
		int in_fd;
		int rgn_size;
#ifdef __USE_LARGEFILE64
		struct stat64 stat_buf;
#else
		struct stat stat_buf;
#endif
		struct region_header region_header;
		unsigned char file_buf[FILE_BUF_SIZE];
		size_t bytes_read;

		/* Get file size */
#ifdef __USE_LARGEFILE64
		if (stat64(regions[i]->file, &stat_buf)) {
#else
		if (stat(regions[i]->file, &stat_buf)) {
#endif
			fprintf(stderr, "Could not stat region file %s: %s\n",
							regions[i]->file,
							strerror(errno));
			exit(1);
		}
		regions[i]->size = stat_buf.st_size;

		in_fd = open(regions[i]->file, O_RDONLY);
		if (in_fd < 0) {
			fprintf(stderr, "Could not open region file %s: %s\n",
							regions[i]->file,
							strerror(errno));
			exit(1);
		}

		region_header.id = regions[i]->id;
		region_header.delay = regions[i]->delay;
		region_header.size = regions[i]->size;
		memcpy(buf, &region_header, sizeof(region_header));

		rgn_size = sizeof(region_header) + regions[i]->size;

		/* Write record header and region header */
		write_record(out_fd, rgn_size, REGION_REC_CHAR, buf,
							sizeof(region_header));

		do {
			bytes_read = read(in_fd, file_buf, FILE_BUF_SIZE);
			if (bytes_read < 0) {
				fprintf(stderr, "File read error in %s: %s\n",
						regions[i]->file,
						strerror(errno));
				exit(1);
			}
			writeall(out_fd, file_buf, bytes_read);
		} while (bytes_read);

		close(in_fd);
	}
	close(out_fd);

	for (i = 0; i < region_count; i++)
		free(regions[i]);
	free(regions);

	return 0;
}

