#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static struct vr_header_v2 {
    unsigned int virtual_region;
    unsigned int header_len;
    unsigned int target;
    unsigned int offset;
    unsigned int chunk_size;
    unsigned int sig_size;
} header;


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


static void
readall (int fd, void *buf, size_t count)
{
    size_t bytes_read;

    do {
        bytes_read = read(fd, buf, count);
        if (bytes_read < 0) {
            fprintf(stderr, "Error reading: %s\n", strerror(errno));
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


static int
readall2 (int fd, void *buf, size_t count)
{
    size_t bytes_read;
    size_t total = 0;

    do {
        bytes_read = read(fd, buf, count - total);
        if (bytes_read < 0) {
            fprintf(stderr, "Error reading: %s\n", strerror(errno));
            exit(1);
        }
        if (bytes_read == 0) {
            break;
        }
        buf += bytes_read;
        total += bytes_read;
    } while (total < count);

    return total;
}


static void
writeall (int fd, const void *buf, size_t count)
{
    size_t bytes_written;

    do {
        bytes_written = write(fd, buf, count);
        if (bytes_written < 0) {
            fprintf(stderr, "Error writing: %s\n", strerror(errno));
            exit(1);
        }
        buf += bytes_written;
        count -= bytes_written;
    } while (count);
}


int main (int argc, char **argv)
{
    void *data;
    void *sig;

    readall (0, &header, sizeof (header));

    lseek (0, header.header_len, SEEK_SET);

    data = xmalloc (header.chunk_size);
    sig = xmalloc (header.sig_size);

    while (1) {
        int bytes;

        /* Read data */
        bytes = readall2 (0, data, header.chunk_size);
        if (bytes < header.chunk_size) {
            if (bytes == 0)
                break;
            if (bytes < header.sig_size) {
                fprintf (stderr, "File format error\n");
                exit (1);
            }
            writeall (1, data, bytes - header.sig_size);
            break;
        }

        /* Read signature */
        bytes = readall2 (0, sig, header.sig_size);
        if (bytes < header.sig_size) {
            writeall (1, data, header.chunk_size + bytes - header.sig_size);
            break;
        }

        /* Write data */
        writeall (1, data, header.chunk_size);
    }

    return 0;
}

