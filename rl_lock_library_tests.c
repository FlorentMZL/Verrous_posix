#include "rl_lock_library.h"

#define N 32

static char *test_file_name = "loremipsum.txt";

#include <pthread.h>

void *read_thread(void *arg)
{
    rl_descriptor rl_fd = *(rl_descriptor *)arg;
    struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
    int return_value = rl_fcntl(rl_fd, F_SETLKW, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    char buffer[N + 1];
    ssize_t bytes_read = rl_read(rl_fd, buffer, N);
    if (bytes_read == -1)
    {
        error("could not read file\n");
    }
    else
    {
        ok("buffer = '%s'\n", buffer); // DEBUG
    }
    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd, F_SETLKW, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    pthread_exit(NULL);
}

void *write_thread(void *arg)
{
    rl_descriptor rl_fd = *(rl_descriptor *)arg;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
    int return_value = rl_fcntl(rl_fd, F_SETLKW, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    ssize_t bytes_written = rl_write(rl_fd, "hello, world!", strlen("hello, world!"));
    if (bytes_written == -1)
    {
        error("could not write file\n");
    }
    else
    {
        ok("bytes_written = %ld\n", bytes_written); // DEBUG
    }
    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd, F_SETLKW, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    pthread_exit(NULL);
}

int test_1_read_then_write()
{
    test("starting test...\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    char buffer[N + 1];
    ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
    buffer[N + 1] = '\0'; 
    if (bytes_read == -1)
    {
        error("could not read file\n");
    }
    else
    {
        test("read %ld bytes\n", bytes_read); // DEBUG
    }

    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    trace("WE CAN WRITE HERE BECAUSE THERE ARE NO LOCKS ON THE FILE\n");
    ssize_t bytes_written = rl_write(rl_fd1, buffer, bytes_read);
    if (bytes_written == -1)
    {
        error("could not write file\n");
    }
    else
    {
        test("%ld bytes written back\n", bytes_written); // DEBUG
    }

    rl_close(rl_fd1);
    return 0;
}

int test_2_read_then_write_blocking()
{
    test("starting test...\n");
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
        char buffer[N + 1];
        // Fill randomly
        for (int i = 0; i < N; i++)
        {
            buffer[i] = 'a' + rand() % 26;
        }
        buffer[N] = '\0';
        ssize_t bytes_written = rl_write(rl_fd2, buffer, N);
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
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
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        sleep(3);
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

int test_3_write_then_read()
{
    test("starting test...\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    char buffer[N + 1];
    ssize_t bytes_written = rl_write(rl_fd1, "hello, world!", strlen("hello, world!"));
    if (bytes_written == -1)
    {
        error("could not write file\n");
    }
    else
    {
        ok("bytes_written = %ld\n", bytes_written); // DEBUG
    }

    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    trace("WE CAN READ HERE BECAUSE THERE ARE NO LOCKS ON THE FILE\n");
    ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
    if (bytes_read == -1)
    {
        error("could not read file\n");
    }
    else
    {
        ok("buffer = '%s'\n", buffer); // DEBUG
    }

    rl_close(rl_fd1);
    return 0;
}

int test_4_write_then_read_blocking()
{
    test("starting test...\n");
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
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        char buffer[N + 1];
        ssize_t bytes_read = rl_read(rl_fd2, buffer, N);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        rl_close(rl_fd2);
    }
    else if (pid > 0)
    {
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        ssize_t bytes_written = rl_write(rl_fd1, "hello, world!", strlen("hello, world!"));
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
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

int test_5_concurrent_reads()
{
    test("starting test...\n");
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
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        char buffer[N + 1];
        ssize_t bytes_read = rl_read(rl_fd2, buffer, N);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
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
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
        waitpid(pid, NULL, 0);
    }
    else
    {
        return -1;
    }
    return 0;
}

int test_6_concurrent_writes()
{
    test("starting test...\n");
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
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
        }
        rl_close(rl_fd2);
    }
    else if (pid > 0)
    {
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        ssize_t bytes_written = rl_write(rl_fd1, "hello, world!", strlen("hello, world!"));
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
        }
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
        waitpid(pid, NULL, 0);
    }
    else
    {
        return -1;
    }
    return 0;
}

int test_7_lock_upgrade()
{
    test("starting test...\n");
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
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        lock.l_type = F_WRLCK;
        return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not upgrade lock\n");
        ok("lock upgraded\n"); // DEBUG
        rl_close(rl_fd2);
    }
    else if (pid > 0)
    {
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        sleep(5);
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
        waitpid(pid, NULL, 0);
    }
    else
    {
        return -1;
    }
    return 0;
}

int test_8_concurrent_reads_and_writes_with_threads()
{
    test("starting test...\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    pthread_t read_thread_id, write_thread_id;

    if (pthread_create(&read_thread_id, NULL, read_thread, &rl_fd1) != 0)
    {
        error("failed to create read thread\n");
        return -1;
    }

    if (pthread_create(&write_thread_id, NULL, write_thread, &rl_fd1) != 0)
    {
        error("failed to create write thread\n");
        return -1;
    }

    if (pthread_join(read_thread_id, NULL) != 0)
    {
        error("failed to join read thread\n");
        return -1;
    }

    if (pthread_join(write_thread_id, NULL) != 0)
    {
        error("failed to join write thread\n");
        return -1;
    }

    rl_close(rl_fd1);
    return 0;
}

int test_9_concurrent_reads_and_writes_with_forks()
{
    test("starting test...\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    int pid1 = rl_fork();
    if (pid1 == 0)
    {
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        ssize_t bytes_written = rl_write(rl_fd1, "hello, world!", strlen("hello, world!"));
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
        }
        rl_close(rl_fd1);
        exit(0);
    }
    int pid2 = rl_fork();
    if (pid2 == 0)
    {
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        char buffer[N + 1];
        ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
        exit(0);
    }
    int status;
    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);
    rl_close(rl_fd1);
    return 0;
}

int test_10_ultimate_test()
{
    test("starting test...\n");

    // Open the file
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    // Duplicate the file descriptor
    rl_descriptor rl_fd2 = rl_dup(rl_fd1);
    if (rl_fd2.file_descriptor < 0)
    {
        error("couldn't duplicate file descriptor\n");
        rl_close(rl_fd1);
        return -1;
    }
    debug("duplicated file descriptor = %d\n", rl_fd2.file_descriptor); // DEBUG

    // Create a thread for reading
    pthread_t read_thread_id;
    if (pthread_create(&read_thread_id, NULL, read_thread, &rl_fd1) != 0)
    {
        error("failed to create read thread\n");
        rl_close(rl_fd1);
        rl_close(rl_fd2);
        return -1;
    }

    // Fork a child process for writing
    int pid = rl_fork();
    if (pid == 0)
    {
        // Write operation in the child process
        struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        ssize_t bytes_written = rl_write(rl_fd2, "hello, world!", strlen("hello, world!"));
        if (bytes_written == -1)
        {
            error("could not write file\n");
        }
        else
        {
            ok("bytes_written = %ld\n", bytes_written); // DEBUG
        }
        rl_close(rl_fd2);
    }
    else if (pid > 0)
    {
        // Read operation in the parent process
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
        int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        char buffer[N + 1];
        ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        lock.l_type = F_UNLCK;
        return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        rl_close(rl_fd1);
    }
    else
    {
        // Error forking a child process
        error("fork failed\n");
        rl_close(rl_fd1);
        rl_close(rl_fd2);
        return -1;
    }

    // Wait for the read thread to finish
    if (pthread_join(read_thread_id, NULL) != 0)
    {
        error("failed to join read thread\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        int test_number = atoi(argv[1]);
        int r;
        switch (test_number)
        {
        case 1:
            r = test_1_read_then_write();
            break;
        case 2:
            r = test_2_read_then_write_blocking();
            break;
        case 3:
            r = test_3_write_then_read();
            break;
        case 4:
            r = test_4_write_then_read_blocking();
            break;
        case 5:
            r = test_5_concurrent_reads();
            break;
        case 6:
            r = test_6_concurrent_writes();
            break;
        case 7:
            r = test_7_lock_upgrade();
            break;
        case 8:
            r = test_8_concurrent_reads_and_writes_with_threads();
            break;
        case 9:
            r = test_9_concurrent_reads_and_writes_with_forks();
            break;
        default:
            error("Invalid test number\n");
            return EXIT_FAILURE;
        }
        if (r == 0)
        {
            test("test passed successfully\n");
        } else 
        {
            error("test failed\n");
        }
        return r;
    }
    else
    {
        test("Running all tests...\n");
        int test_result = 0;
        pthread_t thread;
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_1_read_then_write, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_2_read_then_write_blocking, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_3_write_then_read, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_4_write_then_read_blocking, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_5_concurrent_reads, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_6_concurrent_writes, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_7_lock_upgrade, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_8_concurrent_reads_and_writes_with_threads, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void *)&test_9_concurrent_reads_and_writes_with_forks, NULL);
        pthread_join(thread, NULL);
        if (test_result == 0)
        {
            test("All tests passed!\n");
        }
        else
        {
            test("Some tests failed!\n");
        }
    }
    return 0;
}