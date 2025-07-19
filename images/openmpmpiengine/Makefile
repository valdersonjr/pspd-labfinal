CC=gcc
CFLAGS=-Wall -fopenmp -pthread
LIBS=-lpthread

engine: http_server.c
	$(CC) $(CFLAGS) -o engine http_server.c $(LIBS)

clean:
	rm -f engine