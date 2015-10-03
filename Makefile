CC=gcc
CFLAGS=-c -Wall -g
LDFLAGS=-levent -lapr-1
SOURCES=$(wildcard src/*.c)
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=servertest

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *.o src/*.o servertest example-producer

example-producer: example-producer.o
	$(CC) $(LDFLAGS) example-producer.o -o $@