PREFIX  ?= $(HOME)/.local
BIN     = racecar

.PHONY: all build clean install uninstall local

# Default: build Go orchestrator and install
all: build install

build:
	go build -o $(BIN) .

install: build
	install -d $(PREFIX)/bin
	install -m 755 $(BIN) $(PREFIX)/bin/

clean:
	rm -f $(BIN) src/*.o sentinel/src/*.o sentinel/sentinel

uninstall:
	rm -f $(PREFIX)/bin/$(BIN)

# Build standalone C tools locally (no Daytona needed)
local:
	$(CC) -O3 -Wall -Wextra -std=c11 -march=native -ffast-math \
		-o racecar-local src/main.c src/db.c src/table.c src/vector.c \
		src/hnsw.c src/json.c src/tokenizer.c src/util.c -lm
	cd sentinel && $(MAKE)
