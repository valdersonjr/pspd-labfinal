CC=gcc
CFLAGS=-Wall -pthread
LIBS=-lpthread

socketserver: socketserver_http.c
	$(CC) $(CFLAGS) -o socketserver socketserver_http.c $(LIBS)

clean:
	rm -f socketserver