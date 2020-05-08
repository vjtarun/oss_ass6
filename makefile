CC=gcc
CFLAGS=-Wall -g -pedantic

default: master user

master: master.c oss.o bitmap.o
	$(CC) $(CFLAGS) master.c oss.o bitmap.o -o master -lrt

user: user.c oss.o
	$(CC) $(CFLAGS) user.c oss.o -o user

clean:
	rm -f master user master.log *.o
