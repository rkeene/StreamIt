CC = gcc
CFLAGS = -Wall
OBJS = net.o streamit.o
BINS = streamit

PREFIX = /usr/local
prefix = $(PREFIX)

streamit: net.o streamit.o
streamit.o: net.h streamit.c
net.o: net.c net.h

.PHONY: clean install
install: streamit
	cp streamit $(prefix)/bin

clean:
	rm -f $(OBJS) $(BINS)
