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

int test_2_fusion_division_lock()
{
    info("starting test 2: fusion lock\n");
    rl_descriptor rl_fd1 = rl_open(test_file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file\n");
        return -1;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};    
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    struct flock lock2 = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 5, .l_len = 15};
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock2);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    struct flock lock3 = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 10, .l_len = 5};
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock3);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    char buffer[6];
    ssize_t bytes_written = rl_read(rl_fd1, buffer, 5);
    buffer[5] = '\0';
    if (bytes_written == -1)
    {
        error("could not write file\n");
    }
    else
    {
        ok("read : = %s\n", buffer); // DEBUG
    }
    char buffer2[14];
    bytes_written = rl_read(rl_fd1, buffer2, 13);
    buffer2[13] = '\0';
    if (bytes_written == -1)
    {
        error("could not read file\n");
    }
    else
    {
        ok("read : = %s\n", buffer2); // DEBUG
    }
    rl_close(rl_fd1);
    return 1; 
}

int test_3_fork()
{
    info("starting test 3: fork\n");
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
    int pid = rl_fork();
    if (pid == 0)
    {
        char buffer[N + 1];
        ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
        lseek(rl_fd1.file_descriptor, 0, SEEK_SET);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
       
        return 0;
    }
    else
    {
        char buffer[N + 1];
        lseek(rl_fd1.file_descriptor, 0, SEEK_SET);
        ssize_t bytes_read = rl_read(rl_fd1, buffer, N);
        if (bytes_read == -1)
        {
            error("could not read file\n");
        }
        else
        {
            ok("buffer = '%s'\n", buffer); // DEBUG
        }
        wait(NULL);
    rl_close(rl_fd1);
    }
    
    return 0;
}

int test_4_promoting()
{
    info("starting test 4: promoting\n");
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
    struct flock lock2 ={.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = N};
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock2);
    if (return_value == -1)
        error("could not set lock\n");
    ok("lock set\n"); // DEBUG
    char buffer2[13]; 
    int bytes_written = rl_read(rl_fd1, buffer2, 13);
    buffer2[13] = '\0';
    if (bytes_written == -1)
    {
        error("could not read file\n");
    }
    else
    {
        ok("read : = %s\n", buffer2); // DEBUG
    }
    rl_close(rl_fd1);
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
            test("could not read file [expected]\n");
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
        switch (test_number) {
            case 1:
                return test_1_read_then_write(); 
            case 2:
                return test_2_fusion_division_lock();
            case 3:
                return test_3_fork();
            case 4:
                return test_4_promoting();
            case 5:
                return test_5_concurrent_reads();
            case 6:
                return test_6_concurrent_writes();
            case 7:
                return test_7_lock_upgrade();
            case 8:
                return test_8_concurrent_reads_and_writes_with_threads();
            case 9:
                return test_9_concurrent_reads_and_writes_with_forks();
            default:
                printf("Invalid test number\n");
                break;
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
        pthread_create(&thread, NULL, (void*) &test_2_fusion_division_lock, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void*) &test_3_fork, NULL);
        pthread_join(thread, NULL);
        // Run all tests sequentially
        pthread_create(&thread, NULL, (void*) &test_4_promoting, NULL);
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