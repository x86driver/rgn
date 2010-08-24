prefix = /usr
bindir = $(prefix)/bin

.PHONY: all

all: build-region parse-region bin2c build-signed-update.sh extract-signed-update region-file-data-extractor

build-region: build-region.o
	$(CC) $(CFLAGS) -o build-region build-region.o

build-region.o: build-region.c
	$(CC) $(CFLAGS) -g -c build-region.c

parse-region: parse-region.o
	$(CC) $(CFLAGS) -g -o parse-region parse-region.o

parse-region.o: parse-region.c
	$(CC) $(CFLAGS) -Wall -Werror -g -c parse-region.c

extract-signed-update: extract-signed-update.o
	$(CC) $(CFLAGS) -o extract-signed-update extract-signed-update.o

extract-signed-update.o: extract-signed-update.c
	$(CC) $(CFLAGS) -Wall -Werror -g -c extract-signed-update.c

region-file-data-extractor: region-file-data-extractor.o
	$(CC) $(CFLAGS) -g -o region-file-data-extractor region-file-data-extractor.o

region-file-data-extractor.o: region-file-data-extractor.c
	$(CC) $(CFLAGS) -g -c region-file-data-extractor.c

bin2c: bin2c.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	-rm *.o build-region bin2c parse-region region-file-data-extractor

install: all
	install -d -m 0755 $(DESTDIR)$(bindir)
	install -m 0755 bin2c $(DESTDIR)$(bindir)/bin2c
	install -m 0755 build-region $(DESTDIR)$(bindir)/build-region
	install -m 0755 parse-region $(DESTDIR)$(bindir)/parse-region
	install -m 0755 build-signed-update.sh $(DESTDIR)$(bindir)/build-signed-update.sh
