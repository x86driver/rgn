#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DATA_VERSION_TYPE       'D'
#define APP_VERSION_TYPE        'A'
#define REGION_TYPE             'R'

/* Version identification record */
struct vir {
        unsigned int file_id;
        unsigned short version;
} __attribute__ ((__packed__));

/* Data record */
struct data_record {
        unsigned int size;
        unsigned char type;
} __attribute__ ((__packed__));

/* Application data version record */
struct advr {
        unsigned short version;
} __attribute__ ((__packed__));

/* Application version record */
struct avr {
        unsigned short version;
        char *builder;
        char *build_date;
        char *build_time;
} __attribute__ ((__packed__));

/* Region record */
struct region {
        unsigned short id;
        unsigned int delay;
        unsigned int size;
} __attribute__ ((__packed__));

unsigned char *ptr;

void parse_vir_header()
{
        struct vir *vir_header = (struct vir*)ptr;
        printf("file_id: 0x%x\n", vir_header->file_id);
        printf("version: %d\n", vir_header->version);
        ptr += sizeof(struct vir);
}
void parse_advr(unsigned int size)
{
	struct advr *advr = (struct advr*)ptr;
	printf("Application version: %u.%02u\n", advr->version / 100, advr->version % 100);
	ptr += sizeof(struct advr);
}

void parse_avr(unsigned int size)
{
	struct avr *avr = (struct avr*)ptr;
	printf("Version: %d\n", avr->version);
	printf("builder: %s\n", &avr->builder);
	printf("date: %s\n", &avr->build_date);
	printf("time: %s\n", &avr->build_time);
}

void parse_region(unsigned int size)
{
}

void parse_data_record()
{
	struct data_record *data = (struct data_record*)ptr;
	printf("size: %d, type: %c ", data->size, data->type);
	ptr += sizeof(struct data_record);
	switch (data->type) {
		case DATA_VERSION_TYPE:
			printf("DATA_VERSION_TYPE\n");
			parse_advr(data->size);
			break;
		case APP_VERSION_TYPE:
			printf("APP_VERSION_TYPE\n");
			parse_avr(data->size);
			break;
		case REGION_TYPE:
			printf("REGION_TYPE\n");
			parse_region(data->size);
			break;
		default:
			printf("Error on parsing data\n");
			break;
	}
}

void parse_rgn()
{
	parse_vir_header();
	parse_data_record();
	parse_data_record();
	parse_data_record();
}

int main(int argc, char **argv)
{
#if 0
	if (argc != 3) {
		printf("Usage: %s [rgn file] [file size]\n", argv[0]);
		exit(1);
	}

	int fd = open(argv[1], O_RDONLY);
#endif
	int fd = open("a.rgn", O_RDONLY);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

//	int length = atoi(argv[2]);
	int length = 67;

	unsigned char *buf = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
	if ((void*)buf == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}

	ptr = buf;
	parse_rgn();

	munmap(buf, length);
	close(fd);

	return 0;
}
