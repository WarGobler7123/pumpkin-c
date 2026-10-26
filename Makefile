CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

all: pumpkin-c

pumpkin-c: src/main.c
	$(CC) $(CFLAGS) -o pumpkin-c src/main.c

test: pumpkin-c
	python3 tests/test_status.py

clean:
	rm -f pumpkin-c

.PHONY: all test clean
