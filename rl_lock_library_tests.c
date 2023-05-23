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

    trace("WE CAN WRITE HERE BECAUSE THERE ARE NO LOCKS ON THE FILE\n");
    ssize_t bytes_written = rl_write(rl_fd1, buffer, bytes_read);
    if (bytes_written == -1) {
        error("could not write file\n");
    } else {
        ok("bytes_written = %ld\n", bytes_written); // DEBUG
    }

    rl_close(rl_fd1);
    return 0;
}

int test_2_read_then_write_blocking()
{
    info("starting test 2: read then write - blocking\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG
    rl_descriptor rl_fd2 = rl_dup(rl_fd1);
    int pid = rl_fork();
    if (pid == 0)
    {
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        ssize_t bytes_written = rl_write(rl_fd2, "hello, world!", strlen("hello, world!"));
        if (bytes_written == -1) {
            error("could not write file\n");
        } else {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
        }
        rl_close(rl_fd2);
    }
    else if (pid > 0)
    {
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
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
        sleep(10);
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
    }
    else
    {
        return -1;
    }
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
            case 2:
                return test_2_read_then_write_blocking();
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