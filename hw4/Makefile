CC = gcc
EXTRA_CFLAGS ?= -fsanitize=address -fno-omit-frame-pointer

CFLAGS += -Wall -g -O2 -pthread $(EXTRA_CFLAGS)
LDFLAGS += -pthread $(EXTRA_CFLAGS)

ZIP_NAME ?= $(notdir $(CURDIR)).zip

all: prod-cons

prod-cons: prod-cons.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f prod-cons *.o

zip:
	rm -f $(ZIP_NAME)
	zip -9 $(ZIP_NAME) Makefile *.c

.PHONY: all clean zip
