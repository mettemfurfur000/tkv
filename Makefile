CC = gcc
AR = ar

# this trick is required for clang, since it doesn't really know what to do with unix paths on windows
ifeq ($(OS),Windows_NT)
    MSYSINSTALLDIR := $(shell cd /;pwd -W)
    ROOTDIR := $(MSYSINSTALLDIR)$(CURDIR)
	PREFIX ?= $(MSYSINSTALLDIR)/usr/local
else
    ROOTDIR := $(CURDIR)
	PREFIX ?= /usr/local
endif

TEMLIB_PREFIX ?= $(PREFIX)

CFLAGS ?= -std=c11 -O2 -Wall -Wextra
CPPFLAGS += -Iinclude -I"$(TEMLIB_PREFIX)/include"
CFLAGS += -MMD

SOURCES := \
	src/tkv.c \
	src/tkv_parse.c \
	src/tkv_serialize.c \
	src/tkv_object.c \
	src/tkv_stream.c
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
STATIC_LIB := build/libtkv.a
TEST_BIN := build/test_tkv

-include $(OBJECTS:.o=.d)


.PHONY: all clean install test

all: $(STATIC_LIB)

build/%.o: src/%.c src/tkv_internal.h include/tkv.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/test_tkv.c $(STATIC_LIB) include/tkv.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) -L"$(TEMLIB_PREFIX)/lib" -ltemlib -lbacktrace -lz -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

install: $(STATIC_LIB)
	install -d "$(PREFIX)/include/tkv" "$(PREFIX)/lib"
	install -m 644 include/tkv.h "$(PREFIX)/include/tkv/"
	install -m 644 $(STATIC_LIB) "$(PREFIX)/lib/"

clean:
	rm -rf build
