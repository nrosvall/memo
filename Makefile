DESTDIR?=
PREFIX?=/usr/local
MANPREFIX?=$(PREFIX)/man

CFLAGS+=-std=c99 -Wall

ifeq ($(OS),Windows_NT)
LDFLAGS+=-lpcre
endif

all: memo

clean:
	rm -f memo *.o

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MANPREFIX)/man1
	install -m755 memo $(DESTDIR)$(PREFIX)/bin/
	install -m644 memo.1 $(DESTDIR)$(MANPREFIX)/man1/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/memo
	rm -f $(DESTDIR)$(MANPREFIX)/man1/memo.1

.PHONY: all clean install uninstall
