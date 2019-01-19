CROSS	?= 
CC		:= $(CROSS)-gcc
CFLAGS	:= -O2 -g

all:liblwefence.so test

clean:
	rm -rf liblwefence.so test

liblwefence.so:lwefence.c
	$(CC) $(CFLAGS) -fPIC -shared -c $< -o $@

test:test.c
	$(CC) $< -o $@ -llwefence -L.
