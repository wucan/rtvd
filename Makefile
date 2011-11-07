PROG=rtvd
CFLAGS=	-W -Wall -I. -g
LDFLAGS=-ldl -lpthread
CC = gcc


all:
	$(CC) $(CFLAGS) message.c udp.c webserver.c web_cgi_stati.c stream_page.c mongoose.c  -o $(PROG) $(LDFLAGS)
