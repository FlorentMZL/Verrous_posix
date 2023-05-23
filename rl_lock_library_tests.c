#include "rl_lock_library.h"

#define N 32

static char* test_file_name = "loremipsum.txt";

int test_1_read_then_write()
{
    info("starting test 1: read then write\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG
    
    struct flock lock = { .l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N };
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    
    char buffer[N + 1];
    ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
    if (bytes_read == -1) {
        error("could not read file\n");
    } else {
        ok("buffer = '%s'\n", buffer); // DEBUG
    }
    
    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    ssize_t bytes_written = rl_write(rl_fd1, buffer, bytes_read);
    if (bytes_written == -1) {
        error("could not write file\n");
    } else {
        ok("bytes_written = %ld\n", bytes_written); // DEBUG
    }

    rl_close(rl_fd1);
    return 0;
}

int main(int argc, char** argv)
{
    rl_init_library();
    if (argc == 1) {
        // Run all tests
    } else if (argc == 2) {
        // Run test number argv[1]
        int test_number = atoi(argv[1]);
        switch (test_number) {
            case 1:
                return test_1_read_then_write();
            default:
                error("test number %d not found\n", test_number);
                return -1;
        }
    } else {
        error("too many arguments\n");
        return -1;
    }
    return 0;
}