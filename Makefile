CC=gcc
CFLAGS=-c -Wall
LDFLAGS=-lhttp_parser -levent
SOURCES=main.c conn.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=servertest

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *.o servertest