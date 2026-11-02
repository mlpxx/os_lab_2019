CC=gcc
CFLAGS=-I. -pthread -Wall -Wextra

all : client server

client : client.c libcommon.a common.h
	$(CC) -o client client.c -L. -lcommon $(CFLAGS)

server : server.c libcommon.a common.h
	$(CC) -o server server.c -L. -lcommon $(CFLAGS)

# общий код клиента и сервера собирается в статическую библиотеку
libcommon.a : common.o
	ar rcs libcommon.a common.o

common.o : common.c common.h
	$(CC) -o common.o -c common.c $(CFLAGS)

clean :
	rm -f common.o libcommon.a client server