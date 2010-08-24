/*
 * region-file-data-extractor.c
 *
 * A program to parse region file and dump required chunks
 *
 * Copyright 2009-2010 by Garmin Ltd. or its subsidiaries
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>


#define END_OF_TRANSFER 	(0xFFFFFFFF)

#define IO_READ_RETRY_COUNT	1000
#define IO_WRITE_RETRY_COUNT	1000


/* Region file header information */
#define FILE_ID                 0x7247704B
#define DATA_VERSION_REC_CHAR   'D'
#define APP_VERSION_REC_CHAR    'A'
#define REGION_REC_CHAR         'R'

#define PRODUCT_VERSION_MAJOR   (2)
#define PRODUCT_VERSION_MINOR   (0)
#define LOW_LEVEL_VERSION       100
#define DATA_VERSION            100
#define APP_VERSION             ((PRODUCT_VERSION_MAJOR * 100) \
                                        + PRODUCT_VERSION_MINOR )
/* Build stamps can be defined in a makefile.  Otherwise use these defaults. */
#ifndef BUILD_DATE
#define BUILD_DATE __DATE__
#endif
#ifndef BUILD_TIME
#define BUILD_TIME __TIME__
#endif
#ifndef BUILD_UID
#define BUILD_UID "SQA"
#endif

/* Memory parameters */
#define FILE_BUF_SIZE (4096)
#define REGION_ALLOC_NUM 10
#define RECORD_BUFFER_SIZE 256
#define ERROR_SIZE (70)

#define NUM_DATA_RECS		(7)

#define PARSER_VERSION		"1.0"
#define PARSER_NAME		"Garmin Region File Parser"


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

struct ll_header{
	unsigned int fileid;
	unsigned short version;
}__attribute__ ((__packed__));


struct data_record{
	unsigned int size;
	char type;
	char *data;
}__attribute__ ((__packed__));


struct app_version {
	unsigned short version;
	char *str;
}__attribute__ ((__packed__));


struct pgp_region_hdr {
        unsigned int virt_region_type;   /* Type of virtual region */
        unsigned int header_len;         /* Length of this header */
        unsigned int target;             /* Product-specific field to specify different
                                   areas of flash */
        unsigned int offset;             /* Offset within this flash target */
        unsigned int chunk_size;         /* Size of each signed data chunk */
        unsigned int sig_size;           /* Size each signature is padded to */
}__attribute__ ((__packed__));


/* Macro to build a buffer of null-terminated strings */
#define ADD_STRING(buf, str, max, size) \
        do {                                            \
                strncpy(&buf[size], str, max-size);     \
                size += strlen(str) + 1;                \
        } while (0)



static int parse_cmdline(int argc, char *argv[]);
static void usage(int exitval);
static int init_parser();
static int deinit_parser();
static int parse_rgn_file(int fd);
int parse_rgn_chunks(int fd, int rgn_size, struct pgp_region_hdr);
static int dump_data_sig_to_files(char *data, int data_size, char *sig, 
                                int sig_size, int rgnid, int chunkid);
int read_data(int fd, const char *buff, int size, int giveup);
int write_data(int fd, const char *buff, int size, int giveup);
int get_ll_header(int fd, char *buf, int bufsize);
int read_data_record(int fd, char *buf, int bufsize);
static void dump_bytes(char *data, int num_of_bytes);


int desired_rgn = -1;
int desired_chunk = -1;
int detach_sig = 0;
int verify = 0;

char ofile[512];
char ifile[512];

static int infd;
static int outfd;

static int cur_pos_in_rgn_file = 0;


#define logmsg(format, args...) fprintf(stdout, format, ##args); \
                                         fflush(stdout);


int main(int argc, char *argv[])
{
	int ret;

	printf("\n%s %s\n\n", PARSER_NAME, PARSER_VERSION);
	if(argc > 1) { 
		parse_cmdline(argc, argv);
	} else {
		usage(1);
	}

	ret = init_parser();
	if(ret < 0) {
		logmsg("parser init failed\n");
	}

	ret = parse_rgn_file(infd);
	if(ret < 0) {
		logmsg("%s parse error\n", ifile);
	}

	if(ret < 0) {
		exit(1);
	}

	ret = deinit_parser();	
	if(ret < 0) {
		logmsg("parser deinit failed\n");
	}
	
	
	return 0;
}


static void usage(int exitval)
{
        printf("Usage: rgnfilter [OPTION] [VALUE] rgnfile\n");
        printf("Region file filter tool.\n\n");
        printf("     -h, 	help\n");
        printf("     -r, 	filter for desired region (target or partition) number\n");
        printf("     -c, 	filter for desired chunk within region\n");
	printf("     -d,        detach chunk data and signature\n");
	printf("     -v,        verify gpg signature as the region file gets parsed\n");
	printf("     -o,	output file name\n");
        printf("\n");

        exit(exitval);
}


static int parse_cmdline(int argc, char *argv[])
{
        int option;
	int ofile_provided = 0;

        while((option = getopt(argc, argv, "hdvr:c:o:")) != -1) {
                switch(option) {
                        case 'h':
                                usage(0);
                        break;

                        case 'r':
                                desired_rgn = atoi(optarg);
				printf("Desired region = %d\n", desired_rgn);
                        break;

                        case 'c':
                                desired_chunk = atoi(optarg);
				printf("Desired chunk within region = %d\n", desired_chunk);
                        break;

			case 'd':
				detach_sig = 1;
				printf("Detach data and signature = %d\n", detach_sig);
			break;

			case 'o':
				strcpy(ofile, optarg);
				printf("Output file = %s\n", ofile);
				ofile_provided = 1;
			break;

			case 'v':
				verify = 1;
				printf("Verify signatures = %d\n", verify);
			break;

                        default:
                                printf("Invalid option\n\n");
                                usage(1);
                        break;
                }
        }

	for(; optind < argc; optind++) {
		strcpy(ifile, argv[optind]);
		printf("Input Region File = %s\n", ifile);
		break;
	}

	if(!ofile_provided) {
		snprintf(ofile, sizeof(ofile), "%s.dump", ifile);
	}

	//logmsg("SSIZE_MAX = %d\n", SSIZE_MAX);
	
	printf("\n");

	return 0;
}



static int init_parser()
{
	infd = open(ifile, O_RDONLY);
	if(infd < 0)
		logmsg("infd open err\n");
	outfd = open(ofile, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if(outfd < 0)
                logmsg("outfd open err\n");

	if(infd < 0 || outfd < 0) {
		return -1;
	}

	return 0;
}



static int deinit_parser()
{
	return !(close(infd) && close(outfd));
}


static int parse_rgn_file(int fd)
{
	int done = 0;
	int ret = 0;
	int i = 0;

	char buf[4096];
	struct ll_header *header1;

	if(fd < 0)
		return -1;

	while(!done) {
		ret = get_ll_header(fd, buf, sizeof(buf));
		if(ret) {
			logmsg("ll_header err\n");
			done = 1;
			continue; 
		}

		header1 = (struct ll_header*)buf;
		logmsg("\nLL Header: fileid = %x, version = %i\n", header1->fileid, header1->version);
		for(i = 0; i < NUM_DATA_RECS; i++) {
			ret = read_data_record(fd, buf, sizeof(buf));
                	if(ret) {
				if(ret == -2)
					break;
                        	logmsg("data_record err\n");
                        	done = 1;
                        	continue;
                	}
		}

		done = 1;
	}

	logmsg("Parsing of all regions complete.\n\n");

	return 0;
}


int get_ll_header(int fd, char *buf, int bufsize)
{
        int read_len;
	int ret;

	if(fd < 0 || bufsize < sizeof(struct ll_header))
		return -1;

	read_len = sizeof(struct ll_header);
	//logmsg("read_len = %d\n", read_len);
	ret = read_data(fd, buf, read_len, 1);
	if(ret != read_len)
		ret = -1;
	else
		ret = 0;

	return ret;
}


int read_data_record(int fd, char *buf, int bufsize)
{
        int read_len;
        int ret;
	char type;
        struct data_record *rec;
	struct region_header *rgn_header;
	struct pgp_region_hdr *pgp_hdr;
	int rgn_size;
	struct pgp_region_hdr cur_rgn_pgp_hdr;

        if(fd < 0 || bufsize < sizeof(struct data_record))
                return -1;

        read_len = sizeof(struct data_record) - sizeof(char(*));
        ret = read_data(fd, buf, read_len, 1);
	//logmsg("read_len = %d\n", read_len);
        if(ret != read_len)
                return -1;

	rec = (struct data_record*)buf;

	logmsg("\nData Record: size = %u, type = %c\n", rec->size, rec->type);
	type = rec->type;

	switch(type) {

		case DATA_VERSION_REC_CHAR:
		case APP_VERSION_REC_CHAR:
			read_len = rec->size;
			//logmsg("read_len = %d\n", read_len);
			ret = read_data(fd, buf, read_len, 1);
			if(ret != read_len)
				return -1;
		break;

		case REGION_REC_CHAR:
			read_len = sizeof(struct region_header);
			//logmsg("read_len = %d\n", read_len);
			ret = read_data(fd, buf, read_len, 1);
			if(ret != read_len)
				return -1;
			rgn_header = (struct region_header*)buf;
			rgn_size = rgn_header->size;
			logmsg("\nRegion Header: id = %d, delay = %u, size = %u\n", 
				rgn_header->id, rgn_header->delay, 
				rgn_header->size);
#if 0
			read_len = sizeof(struct pgp_region_hdr);
			//logmsg("read_len = %d\n", read_len);
			//logmsg("filpos = %d\n", cur_pos_in_rgn_file);
                        ret = read_data(fd, buf, read_len, 1);
                        if(ret != read_len)
                                return -1;
			pgp_hdr = (struct pgp_region_hdr*)buf;
			memcpy(&cur_rgn_pgp_hdr, pgp_hdr, sizeof(cur_rgn_pgp_hdr));
			logmsg("\nPGP Header: virtual_rgn_type = %u, header_len = %u, "
                                 "target = %u, offset = %u, chunk_size = %u, sig_size = %u\n", 
                                pgp_hdr->virt_region_type,
			       	pgp_hdr->header_len, pgp_hdr->target, pgp_hdr->offset,
				pgp_hdr->chunk_size, pgp_hdr->sig_size);
			if(pgp_hdr->target == END_OF_TRANSFER) {
				logmsg("last target, nothing beyond!\n");
				return -2;
			}
			/* process chunks inside a region */
			if(desired_rgn == -1 || desired_rgn == pgp_hdr->target) {
				logmsg("\nProcessing region: %u\n", desired_rgn);
				ret = parse_rgn_chunks(fd, rgn_size, cur_rgn_pgp_hdr);
				if(ret < 0)
					logmsg("chunk parsing err\n");
				return -2;

			} else {
				lseek(fd, rgn_size - read_len, SEEK_CUR);
				cur_pos_in_rgn_file += rgn_size - read_len;
			}
#endif
		break;

		default:
			logmsg("invalid rec char\n");
			return -1;
		break;
	}

        return 0;
}



int parse_rgn_chunks(int fd, int rgn_size, struct pgp_region_hdr pgp)
{
	int chunkid = 0;
	int skip_bytes = 0;
	int ret;
	int data_read = 0;

	char data_buf[pgp.chunk_size + 512];
	char sig_buf[pgp.sig_size + 512];

	int rgn_pos = 0;
	
	/* chunks are indexed at 0 */
	/*
  	logmsg("parsing chunks<%u %u %u %u %u %u>\n", 
		pgp.virt_region_type,
		pgp.header_len, pgp.target, pgp.offset, 
		pgp.chunk_size, pgp.sig_size);
	*/

	if(desired_chunk == -1)	{
		/* dump each chunk */
		logmsg("dumping each chunk\n");
		rgn_size -= sizeof(struct pgp_region_hdr);
		rgn_pos = 0;
		chunkid = 0;
		while(rgn_pos < rgn_size 
				- pgp.sig_size) {
			if(rgn_pos + pgp.chunk_size + pgp.sig_size > rgn_size)
				data_read = rgn_size
				- rgn_pos - pgp.sig_size;
			else
				data_read = pgp.chunk_size;
			logmsg("\nDumping chunk <region = %d, chunkid = %d> "
                                 "@ <byte_offset = %d, size = %d>\n\n", pgp.target,
				chunkid, rgn_pos, data_read);
                	ret = read_data(fd, data_buf, data_read, 0);
                	if(ret != data_read) {
                        	logmsg("unable to read pgp data %d\n", ret);
                        	goto cleanup;
                	}

                	/* dump sig */
                	ret = read_data(fd, sig_buf, pgp.sig_size, 0);
                	if(ret != pgp.sig_size) {
                       		logmsg("unable to read pgp sig %d\n", ret);
                       		goto cleanup;
                	}

                	ret = dump_data_sig_to_files(data_buf, data_read, sig_buf,
                                       pgp.sig_size, pgp.target, chunkid);
               		if(ret) {
                       		logmsg("unable to dump data and sig %d\n", ret);
                       		goto cleanup;
               		}
        	       	chunkid++;
			rgn_pos += data_read + pgp.sig_size;	
		}
		
	} else if (desired_chunk >= 0) {

		rgn_size -= sizeof(struct pgp_region_hdr);
		/* dump if desired_chunk lies in this rgn */
		//logmsg("dumping the desired chunk\n");
		skip_bytes = desired_chunk * (pgp.chunk_size + pgp.sig_size); 

		/* chunk not in rgn */
		if(skip_bytes > rgn_size) {
			logmsg("chunk not found in this rgn %d %d\n", 
				skip_bytes, rgn_size);
			chunkid = -1;
			goto cleanup;
		}

		//logmsg("skip_bytes = %d\n", skip_bytes);

		/* seek to the desired chunk */
		lseek(fd, skip_bytes, SEEK_CUR);
		cur_pos_in_rgn_file += skip_bytes;

		//logmsg("writing %d\n", desired_chunk);
		if(skip_bytes + pgp.chunk_size + pgp.sig_size > rgn_size) {
			data_read = rgn_size  - skip_bytes - pgp.sig_size;
		} else {
			data_read = pgp.chunk_size;
		}
                logmsg("\nDumping chunk <region = %d, chunkid = %d> @ <byte_offset = %d, "
                         "size = %d>\n\n", pgp.target, 
			desired_chunk, skip_bytes, data_read);
		ret = read_data(fd, data_buf, data_read, 0);
		if(ret != data_read) {
			logmsg("unable to read pgp data %d\n", ret);
			chunkid = -1;
			goto cleanup;
		} 

                /* dump sig */
                ret = read_data(fd, sig_buf, pgp.sig_size, 0);
                if(ret != pgp.sig_size) {
                        logmsg("unable to read pgp sig %d\n", ret);
			chunkid = -1;
			goto cleanup;
                }

		ret = dump_data_sig_to_files(data_buf, data_read, sig_buf,
					pgp.sig_size, pgp.target, desired_chunk);

		if(ret) {
			logmsg("unable to dump data and sig %d\n", ret);
			chunkid = -1;
			goto cleanup;
		}	
		
		chunkid = desired_chunk;
	}

cleanup:
	return chunkid;

}


static int dump_data_sig_to_files(char *data, int data_size, char *sig, 
				int sig_size, int rgnid, int chunkid)
{
        char datafname[512];
        char sigfname[512];
	char system_cmd[512];
        int datafd;
        int sigfd;
	int ret;

        sprintf(datafname, "%s.%d.%d", ofile, rgnid, chunkid);
        sprintf(sigfname, "%s.%d.%d.sig", ofile, rgnid, chunkid);

        datafd = open(datafname,
                   O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if(datafd < 0) {
        	logmsg("unable to open %s %d\n", datafname, datafd);
		return -1;
        }

        sigfd = open(sigfname,
                    O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
        if(sigfd < 0) {
                logmsg("unable to open %s %d\n", sigfname, sigfd);
		return -1;	
        }

        ret = write_data(datafd, data, data_size, 0);
        if(ret != data_size) {
                logmsg("unable to write to %s %d\n", datafname, ret);
        	return -1;
        }

        ret = write_data(sigfd, sig, sig_size, 0);
	if(ret != sig_size) {
                logmsg("unable to write to %s %d\n", sigfname, ret);
                return -1;
        }

	close(datafd);
        close(sigfd);

        if(verify) {
	        snprintf(system_cmd, sizeof(system_cmd), "gpg --verify %s",
                                sigfname);

                ret = system(system_cmd);
                if(ret) {
                	logmsg("unable to verify\n");
			exit(1);
                }
        }

	return 0;
}


int read_data(int fd, const char *buff, int size, int giveup)
{
        int bytes_left = size;
        int bytes_read = 0;
        int tot_bytes_read = 0;
        int no_read = 0;

        if(fd < 0 || buff == 0) {
                return -1;
        }

        while(bytes_left) {
                bytes_read = read(fd, (void*)(buff + tot_bytes_read), bytes_left);
                if(bytes_read <= 0) {
                        no_read++;
                        if(no_read % IO_READ_RETRY_COUNT == 0) {
                                //logmsg("r%d.", tot_bytes_read);
                                no_read = 0;
                                if(giveup)
                                        break;
                        }
                } else {
                        bytes_left -= bytes_read;
                        tot_bytes_read += bytes_read;
                }
        }

	cur_pos_in_rgn_file += tot_bytes_read;
	//logmsg("filpos = %d\n", cur_pos_in_rgn_file);

        return tot_bytes_read;
}



int write_data(int fd, const char *buff, int size, int giveup)
{
        int bytes_left = size;
        int bytes_written = 0;
        int tot_bytes_written = 0;
        int no_write = 0;

        if(fd < 0 || buff == 0) {
                return -1;
        }

        while(bytes_left) {
                bytes_written = write(fd, buff + tot_bytes_written, bytes_left);
                if(bytes_written <= 0) {
                        no_write++;
                        if(no_write % IO_WRITE_RETRY_COUNT == 0) {
                                logmsg("w%d.", tot_bytes_written);
                                no_write = 0;
                                if(giveup)
                                        break;
                        }
                } else {
                        bytes_left -= bytes_written;
                        tot_bytes_written += bytes_written;
                }
        }

        return tot_bytes_written;
}


static void dump_bytes(char *data, int num_of_bytes)
{
        int pos = 0;

        if(num_of_bytes <= 0) {
                return;
        }

        for(pos = 0; pos < num_of_bytes; pos++) {
                logmsg("0x%x  ", *(data+pos));
                if(pos == 15) {
                        logmsg("\n");
                }
        }
        logmsg("\n");
}
