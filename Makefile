all: rl_lock_library

CC = gcc
CCFLAGS = -pthread -Wall -Wpedantic -g

rl_lock_library: rl_lock_library.c
	$(CC) $(CCFLAGS) -o rl_lock_library rl_lock_library.c

clean:
	rm -f *.o rl_lock_library