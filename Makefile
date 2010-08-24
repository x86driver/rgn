TARGET = parse

all:$(TARGET)

parse:parse.c
	gcc -Wall -o $@ $< -g

clean:
	rm -rf $(TARGET)

