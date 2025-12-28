CC ?= cc
EXTRA_CFLAGS ?=
CFLAGS := -Wall -g $(EXTRA_CFLAGS) -MMD -MP

NSD_SRCS := nsd_main.c nsd.c nd.c
NSD_OBJS := $(NSD_SRCS:.c=.o)
DEPS     := $(NSD_OBJS:.o=.d) forkpipe.d

.PHONY: all clean zip
all: forkpipe nsd

forkpipe: forkpipe.o
	$(CC) $(CFLAGS) -o $@ $^

nsd: $(NSD_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

-include $(DEPS)

zip:
	tar czf osy04.tgz Makefile *.[ch]

clean:
	rm -f forkpipe nsd *.o *.d
