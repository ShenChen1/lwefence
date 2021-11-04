CROSS   ?=
CC      := $(CROSS)-gcc
CFLAGS  := -Os -g -Wall -Werror

all: clean test

clean:
	rm -rf *.so test

liblwefence.so: lwefence.c
	$(CC) $(CFLAGS) -fPIC -shared  $< -o $@ 

test: test.c liblwefence.so
	$(CC) $< -o $@ -llwefence -L.
