CFLAGS?=-O2
CFLAGS+=-std=gnu11 -g -Wall -Wextra
CPPFLAGS:=-Iinclude

LIBS:=libmpdclient
HAS_DEPS:=$(foreach lib,$(LIBS),\
	$(if $(shell pkg-config --exists $(lib) || echo n),\
	$(error $(lib) not found)))
CPPFLAGS+=$(shell pkg-config --cflags $(LIBS))
LDLIBS:=$(shell pkg-config --libs $(LIBS))

SRCDIR:=src
BUILDDIR:=build
SOURCES:=$(wildcard $(SRCDIR)/*.c)
OBJECTS:=$(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

PREFIX?=/usr/local

all: mpdsub

mpdsub: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LOADLIBES) $(LDLIBS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(COMPILE.c) $^ -o $@

$(OBJECTS): |$(BUILDDIR)
$(BUILDDIR):
	@mkdir -p $@

clean:
	@$(RM) -r $(BUILDDIR)/ mpdsub

install: mpdsub
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $^ $(DESTDIR)$(PREFIX)/bin

.PHONY: clean all
