CC = gcc
CFLAGS = -O2 -g -Wall -Wno-unused-function -no-pie -Iinclude
LDFLAGS = -no-pie -ldl -Wl,-Ttext-segment=0x2000000

TARGET = minwin_loader
SRC = src/loader/loader.c

all: $(TARGET)

$(TARGET): $(SRC) include/pe.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
