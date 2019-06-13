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
	install -d $(PREFIX)/bin $(MANPREFIX)/man1
	install -m755 memo $(PREFIX)/bin/
	install -m644 memo.1 $(MANPREFIX)/man1/

uninstall:
	rm -f $(PREFIX)/bin/memo
	rm -f $(MANPREFIX)/man1/memo.1

