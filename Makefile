CC=gcc
CFLAGS=-Wall -Wextra -pedantic -std=c11

all: tftp_server tftp_client

server: server.c
	$(CC) $(CFLAGS) -o tftp_server tftp_server.c

client: client.c
	$(CC) $(CFLAGS) -o tftp_client tftp_client.c

clean:
	rm -f tftp_server tftp_client
