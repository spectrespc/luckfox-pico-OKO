# Makefile for motorctl utility

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -Wno-unused-function -I. -I../src/common -I../src/i2c
TARGET = motorctl
SOURCES = motorctl.c i2c_host.c ../src/common/oko_boot.c ../src/common/oko_crc.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install