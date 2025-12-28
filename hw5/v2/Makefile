CC              ?= gcc
EXTRA_CFLAGS    ?= -fsanitize=address -fno-omit-frame-pointer

CFLAGS          := -Wall -g -O2 -pthread $(EXTRA_CFLAGS)
LDFLAGS         := $(EXTRA_CFLAGS)
LDLIBS          := -pthread

TARGET          := factory
SOURCES         := $(wildcard *.c)
OBJECTS         := $(SOURCES:.c=.o)

ZIPNAME         ?= factory.zip

.PHONY: all clean distclean zip tsan run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

zip: $(ZIPNAME)
$(ZIPNAME): $(SOURCES) Makefile
	rm -f $@
	zip -9 $@ $^

tsan: clean
	$(MAKE) EXTRA_CFLAGS=-fsanitize=thread

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(OBJECTS)

distclean: clean
	$(RM) $(TARGET) $(ZIPNAME)
