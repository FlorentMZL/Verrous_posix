all: rl_lock_library

CC = gcc
CCFLAGS = -pthread -Wall -g

rl_lock_library: rl_lock_library.c rl_lock_library.h
	$(CC) $(CCFLAGS) -o rl_lock_library rl_lock_library.c

clean:
	rm -f *.o rl_lock_library

# Command to run make then valgrind --leak-check=full ./rl_lock_library
# Path: Makefile
vgrind: rl_lock_library
	valgrind --leak-check=full ./rl_lock_library

