CC     = gcc
CFLAGS = -Wall -Wextra -g

all: tftp_server tftp_client

tftp_server: tftp_server.c tftp.c tftp.h
	$(CC) $(CFLAGS) -o $@ tftp_server.c tftp.c

tftp_client: tftp_client.c tftp.c tftp.h tftp_client.h
	$(CC) $(CFLAGS) -o $@ tftp_client.c tftp.c

clean:
	rm -f tftp_server tftp_client