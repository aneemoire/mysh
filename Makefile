
CC = gcc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L

STD = $(shell echo 'int main(void){return 0;}' | $(CC) -std=c23 -x c -fsyntax-only - 2>/dev/null && echo c23 || echo c2x)
WARN = -Wall -Wextra
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)

all: mysh

mysh: $(SRC) $(HDR)
	$(CC) -std=$(STD) $(WARN) -O2 $(CPPFLAGS) $(SRC) -o $@

debug: mysh-debug

mysh-debug: $(SRC) $(HDR)
	$(CC) -std=$(STD) $(WARN) -g -fsanitize=address,undefined $(CPPFLAGS) $(SRC) -o $@

clean:
	rm -f mysh mysh-debug

.PHONY: all debug clean