CROSS	?=
CC		:= $(CROSS)-gcc
CFLAGS	:= -O2 -g -Wall -Werror

all: lwefence.so

clean:
	rm -rf lwefence.so test

%.so: %.c
	$(CC) $(CFLAGS) -fPIC -shared -c $< -o $@ -ldl

test: test.c
	$(CC) $< -o $@ lwefence.so -ldl
