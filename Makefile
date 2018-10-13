CROSS	?= 
CC	:= $(CROSS)-gcc

all:liblwefence.so test

liblwefence.so:lwefence.c
	$(CC) -fPIC -shared -c $< -o $@

test:test.c
	$(CC) $< -o $@ -llwefence -L.

	
