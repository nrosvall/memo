override CFLAGS+=-std=c99 -Wall
PREFIX=/usr/local
LDFLAGS=

ifeq ($(OS),Windows_NT)
LDFLAGS=-lpcre
endif

all: memo

memo: memo.o
	$(CC) $(CFLAGS) memo.o -o memo $(LDFLAGS)

memo.o: memo.c
	$(CC) $(CFLAGS) -c memo.c

clean:
	rm memo
	rm *.o

install: all
	install -d $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -m755 memo $(PREFIX)/bin/
	install -m644 memo.1 $(PREFIX)/share/man/man1/

uninstall:
	rm -f $(PREFIX)/bin/memo
	rm -f $(PREFIX)/share/man/man1/memo.1

