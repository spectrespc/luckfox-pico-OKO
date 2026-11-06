# Host unit tests (gcc). Independent of the firmware build in the repo root.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -I../src/core -I../src/i2c -I../src/motor \
          -I../src/common

TARGETS = test_i2c_protocol test_stepper_logic test_crc32 test_boot_header \
          test_boot_protocol

test: $(TARGETS)
	./test_i2c_protocol
	./test_stepper_logic
	./test_crc32
	./test_boot_header
	./test_boot_protocol
	python3 test_ihx.py

test_i2c_protocol: test_i2c_protocol.c ../src/core/main.h ../src/i2c/i2c_proto.h ../src/motor/stepper_logic.h
	$(CC) $(CFLAGS) -o $@ test_i2c_protocol.c

test_stepper_logic: test_stepper_logic.c ../src/core/main.h ../src/motor/stepper_logic.h
	$(CC) $(CFLAGS) -o $@ test_stepper_logic.c

test_crc32: test_crc32.c ../src/common/oko_crc.h ../src/common/oko_crc.c
	$(CC) $(CFLAGS) -o $@ test_crc32.c ../src/common/oko_crc.c

test_boot_header: test_boot_header.c ../src/common/oko_boot.h ../src/common/oko_boot.c ../src/common/oko_crc.c
	$(CC) $(CFLAGS) -o $@ test_boot_header.c ../src/common/oko_boot.c ../src/common/oko_crc.c

test_boot_protocol: test_boot_protocol.c ../src/common/oko_boot.h ../src/common/oko_boot.c ../src/common/oko_crc.c
	$(CC) $(CFLAGS) -o $@ test_boot_protocol.c ../src/common/oko_boot.c ../src/common/oko_crc.c

clean:
	rm -f $(TARGETS)

.PHONY: test clean