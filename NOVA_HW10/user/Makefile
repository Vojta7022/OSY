ALL = hello pagefault malloc_test

CC = gcc
CFLAGS = -m32 -march=i686 -g -nostdlib -fomit-frame-pointer -fno-stack-protector -no-pie -Og -Wall -Ilib -DNOVA
CXXFLAGS = $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS = -m32 -g -Wa,--noexecstack
LDFLAGS = -Wl,--build-id=none -Wl,--gc-sections -Wl,-Map=$@.map

all: $(ALL)

clean:
	rm -f $(ALL) libc.a $(wildcard *.o) $(wildcard lib/*.o)

# Userspace

LIB_OBJ = console.o console_kernel.o stdio.o start.o init.o
libc.a: $(LIB_OBJ:%=lib/%)
	ar rcs $@ $^

# Add mem_alloc to libc, if it exists
ifneq ($(wildcard mem_alloc.cc mem_alloc.c),)
libc.a: mem_alloc.o
endif

%: %.o libc.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,--script=linker.ld $^ -o $@

# Disable built-in rules

%: %.c
%: %.cc
%: %.o
