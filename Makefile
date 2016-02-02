CC=gcc
TARGET=player
CFLAGS=-Wall -std=c99 $(shell pkg-config --cflags gstreamer-0.10 gtk+-2.0 gstreamer-interfaces-0.10)
LIBS=$(shell pkg-config --libs gstreamer-0.10 gtk+-2.0 gstreamer-interfaces-0.10)

all: main

main : player.o
	$(CC) player.o -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) -o $@ -c $< $(FLAGS) $(CFLAGS) $(INCLUDE)

clean :
	rm -f *.o $(TARGET)
