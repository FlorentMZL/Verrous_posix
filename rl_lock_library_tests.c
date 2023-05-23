#include "rl_lock_library.h"

typedef struct rl_lock_library_tests_args {
    char* file_name;
    size_t id;
} rl_lock_library_tests_args_t;

void* do_stuff(void* arg) 
{   
    rl_lock_library_tests_args_t* args = (rl_lock_library_tests_args_t*) arg;

    /**char log_file[1024];
    int written = snprintf(log_file, 1024, "%s_%ld.log.txt", args->file_name, args->id);
    log_file[written] = '\0';

    int pfd = open(log_file, O_WRONLY | O_CREAT, 0777);
    int saved = dup(1);

    close(1);
    dup(pfd);
    close(pfd);**/

    info("file_name from args = '%s'\n", args->file_name); // DEBUG 

    rl_descriptor rl_fd1 = rl_open((char*) args->file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0)
    {
        error("couldn't open file '%s'\n", args->file_name);
        return NULL;
    }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG

    rl_descriptor rl_fd2 = rl_dup(rl_fd1);



    /*
        // On duplique le descripteur de fichier
        rl_descriptor rl_fd2 = rl_dup(rl_fd1);
        debug("file descriptor = %d\n", rl_fd2.file_descriptor);	// DEBUG

        // On pose un verrou en lecture sur le fichier
        struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_stn,art = 0, .l_len = 10 };
        rl_fcntl(rl_fd1, F_SETLK, &lock);
        debug("lock set\n");	// DEBUG
    */

    /*struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1) error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    // unlock previous lock
    lock.l_type = F_UNLCK;
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1) error("could not set lock\n");
*/
    pid_t pid = rl_fork();
    if (pid == 0)
    {
        struct flock lock2 = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 5};
        int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock2);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        sleep(5);
        struct stat statbuf;
        fstat(rl_fd1.file_descriptor, &statbuf);
        debug("size of file = %ld\n", statbuf.st_size); // DEBUG
        struct flock lock3 = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 5, .l_len = 0};
        return_value = rl_fcntl(rl_fd1, F_SETLK, &lock3);
        if (return_value == -1)
            error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        struct flock lock5 = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 8, .l_len = 2};
         return_value = rl_fcntl(rl_fd1, F_SETLK, &lock5);
        if (return_value == -1) error("could not set lock\n");
        ok("lock set\n"); // DEBUG
        // struct flock lock4 = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};
        // return_value = rl_fcntl(rl_fd1, F_SETLK, &lock4);
        // if (return_value == -1) error("could not set lock\n");
        // ok("lock set\n"); // DEBUG
        // On lit le fichier
        char buffer[6];
        ssize_t re = rl_read(rl_fd1, buffer, 5);
        buffer[5] = '\0';
        if (re == -1)
        {
            error("could not read file\n");
        }
        else
            ok("buffer = '%s'\n", buffer); // DEBUG
        ssize_t wr = rl_write(rl_fd1, "abcde", 5);
        if (wr == -1)
            error("could not write file\n");
        rl_close(rl_fd1);
        ok("file descriptor closed\n"); // DEBUG
    }
    else if (pid < 0)
    {
        error("fork() failed\n");
    }
    else
    {
        /**
        char buffer[10] = "1234567890";
        ssize_t wr = rl_write(rl_fd1, buffer, 10);
        if (wr == -1) error("could not write file\n");
        ok("buffer = '%s'\n", buffer); // DEBUG
        **/
        /*struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 5};
        int return_value = rl_fcntl(rl_fd2, F_SETLK, &lock);
        if (return_value == -1) error("could not set lock\n");

        char buffer[6];
        ssize_t re = rl_read(rl_fd2, buffer, 5);
        buffer[5] = '\0';
        if (re == -1) { error("could not read file\n"); } else ok("buffer = '%s'\n", buffer); // DEBUG
        rl_close(rl_fd2);
        ok("file descriptor closed\n"); // DEBUG
        */
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 5};
        int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
        if (return_value == -1)
            error("could not set lock\n");

        char buffer[6];
        ssize_t re = rl_read(rl_fd2, buffer, 5);
        buffer[5] = '\0';
        if (re == -1)
        {
            error("could not read file\n");
        }
        else
            ok("buffer = '%s'\n", buffer); // DEBUG
        rl_close(rl_fd2);
        ok("file descriptor closed\n"); // DEBUG
    }
    printf("This goes into file\n");
    fflush(stdout);  // <-- THIS
    // restore it back
    dup2(saved, 1);
    close(saved);
    wait(NULL);
    return NULL;
}

int main(int argc, char **argv)
{
    rl_init_library();

    char *file_name;
    if (argc < 2)
        file_name = "test.txt";
    else
        file_name = argv[1];

    #define N 2

    rl_lock_library_tests_args_t args;
    args.file_name = file_name;

    pthread_t threads[N];
    for (size_t i = 0; i < N; i++) {
        args.id = i;
        pthread_create(&threads[i], NULL, do_stuff, (void*) &args);
    }

    for (size_t i = 0; i < N; i++) 
        pthread_join(threads[i], NULL);

    return EXIT_SUCCESS;
}
