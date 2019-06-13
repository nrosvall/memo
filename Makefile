CFLAGS+=-std=c99 -Wall
PREFIX=/usr/local

ifeq ($(OS),Windows_NT)
LDFLAGS+=-lpcre
endif

all: memo

clean:
	rm -f memo *.o

install: all
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -m755 memo $(PREFIX)/bin/
	install -m644 memo.1 $(PREFIX)/share/man/man1/

uninstall:
	rm -f $(PREFIX)/bin/memo
	rm -f $(PREFIX)/share/man/man1/memo.1

