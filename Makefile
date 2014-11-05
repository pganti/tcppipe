CC = gcc

all: tcppiped tcppipe

tcppiped: tcppipe.h tcppiped.c utils.c
	$(CC) -o tcppiped tcppiped.c utils.c

tcppipe: tcppipe.h tcppipe.c utils.c
	$(CC) -o tcppipe tcppipe.c utils.c
clean:
	rm tcppipe
	rm tcppiped

