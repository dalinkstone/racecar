CC      ?= cc
CFLAGS  ?= -O3 -Wall -Wextra -std=c11
CFLAGS  += -march=native -ffast-math
LDFLAGS += -lm

PREFIX  ?= $(HOME)/.local

# Sentinel sources
SENT_SRC = src/main.c src/classify.c src/data.c
SENT_OBJ = $(SENT_SRC:.c=.o)

# Racecar library objects (everything except racecar's main.o)
RC_SRC = ../src/db.c ../src/table.c ../src/vector.c ../src/hnsw.c \
         ../src/json.c ../src/tokenizer.c ../src/util.c
RC_OBJ = $(RC_SRC:.c=.o)

BIN = sentinel

.PHONY: all clean install uninstall racecar-lib

all: racecar-lib $(BIN) install

# Build racecar objects via parent Makefile
racecar-lib:
	$(MAKE) -C .. $(patsubst ../%,%,$(RC_OBJ))

$(BIN): $(SENT_OBJ) $(RC_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c src/classify.h src/data.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SENT_OBJ) $(BIN)

install: $(BIN)
	install -d $(PREFIX)/bin
	install -m 755 $(BIN) $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/$(BIN)
