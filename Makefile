CC = gcc
TARGET = pr

all: $(TARGET)

$(TARGET): my_OS.c
	$(CC) -o $(TARGET) my_OS.c -lrt

clean:
	rm -f $(TARGET)
