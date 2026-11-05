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

OBJECT := build/tkv.o
STATIC_LIB := build/libtkv.a

-include build/tkv.d


.PHONY: all clean install

all: $(STATIC_LIB)

$(OBJECT): src/tkv.c include/tkv.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

install: $(STATIC_LIB)
	install -d "$(PREFIX)/include/tkv" "$(PREFIX)/lib"
	install -m 644 include/tkv.h "$(PREFIX)/include/tkv/"
	install -m 644 $(STATIC_LIB) "$(PREFIX)/lib/"

clean:
	rm -rf build
