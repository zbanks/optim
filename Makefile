#CC=gcc
#CC=clang
#CC=afl-clang-fast

CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Werror -D_POSIX_C_SOURCE=201704L -Isrc/
#CFLAGS += -ggdb3 -O0
CFLAGS += -O3

liboptim.so: src/optim.c
	$(CC) $(CFLAGS) -fPIC -shared $< -o $@

optim_test: test/main.c liboptim.so
	$(CC) $(CFLAGS) -Wl,-rpath='$$ORIGIN' -L. $< -loptim -o $@

.PHONY: clean
clean:
	-rm -f liboptim.so optim_test

.PHONY: all
all: optim_test

.DEFAULT_GOAL = all
