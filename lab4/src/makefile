CC=gcc
CFLAGS=-I.

all : parallel_min_max process_memory zombie

parallel_min_max : utils.o find_min_max.o parallel_min_max.c utils.h find_min_max.h
	$(CC) -o parallel_min_max utils.o find_min_max.o parallel_min_max.c $(CFLAGS)

process_memory : process_memory.c
	$(CC) -w -no-pie -o process_memory process_memory.c

zombie : zombie.c
	$(CC) -o zombie zombie.c $(CFLAGS)

utils.o : utils.c utils.h
	$(CC) -o utils.o -c utils.c $(CFLAGS)

find_min_max.o : find_min_max.c utils.h find_min_max.h
	$(CC) -o find_min_max.o -c find_min_max.c $(CFLAGS)

clean :
	rm -f utils.o find_min_max.o parallel_min_max process_memory zombie