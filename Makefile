CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = myShell
SRCS = myShell.c linenoise.c
HDRS = linenoise.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean

