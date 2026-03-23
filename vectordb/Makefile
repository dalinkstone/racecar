CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lm

all: test_vectordb vectordb_cli

test_vectordb: vectordb.c test_vectordb.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

vectordb_cli: vectordb.c cli.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: test_vectordb
	./test_vectordb

clean:
	rm -f test_vectordb vectordb_cli

.PHONY: all test clean
