CC = gcc
TARGET = myShell

all: $(TARGET)

# Links the object files into the final executable
$(TARGET): myShell.o linenoise.o
	$(CC) myShell.c linenoise.c -o $(TARGET)



clean:
	rm -f *.o $(TARGET)
