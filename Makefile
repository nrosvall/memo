CC=gcc
CFLAGS=-Wall -Werror
PREFIX=/usr/local

all: memo

memo: memo.o
	$(CC) $(CFLAGS) memo.o -o memo

memo.o: memo.c
	$(CC) $(CFLAGS) -c memo.c

clean:
	rm memo
	rm *.o

install:
	cp memo $(PREFIX)/bin/

uninstall:
	rm $(PREFIX)/bin/memo
