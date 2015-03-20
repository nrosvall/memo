CC=gcc
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
	if [ ! -d $(PREFIX)/share/man/man1 ];then	\
		mkdir -p $(PREFIX)/share/man/man1;	\
	fi
	cp memo.1 $(PREFIX)/share/man/man1/
	gzip -f $(PREFIX)/share/man/man1/memo.1
	cp memo $(PREFIX)/bin/

uninstall:
	rm $(PREFIX)/bin/memo
	rm $(PREFIX)/share/man/man1/memo.1.gz

