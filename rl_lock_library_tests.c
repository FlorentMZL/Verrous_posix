#include "rl_lock_library.h"

int main(int argc, char** argv) {
    rl_init_library();

    char* file_name;
    if (argc < 2) file_name = "test.txt"; else file_name = argv[1];
    info("file_name from args = '%s'\n", file_name); // DEBUG

    rl_descriptor rl_fd1 = rl_open(file_name, O_RDWR, S_IRUSR | S_IWUSR);
    if (rl_fd1.file_descriptor < 0) { error("couldn't open file\n"); return -1; }
    debug("file descriptor = %d\n", rl_fd1.file_descriptor); // DEBUG
    
    rl_descriptor rl_fd2 = rl_dup(rl_fd1); 

    /*
        // On duplique le descripteur de fichier
        rl_descriptor rl_fd2 = rl_dup(rl_fd1);
        debug("file descriptor = %d\n", rl_fd2.file_descriptor);	// DEBUG

        // On pose un verrou en lecture sur le fichier
        struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10 };
        rl_fcntl(rl_fd1, F_SETLK, &lock);
        debug("lock set\n");	// DEBUG
    */

    struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};
    int return_value = rl_fcntl(rl_fd1, F_SETLKW, &lock);
    if (return_value == -1) error("could not set lock\n");
    ok("lock set\n"); // DEBUG

    pid_t pid = rl_fork();
    if (pid == 0) {
        // On lit le fichier
        char buffer[6];
        ssize_t re = rl_read(rl_fd1, buffer, 5);
        buffer[5] = '\0';
        if (re == -1) { error("could not read file\n"); } else ok("buffer = '%s'\n", buffer); // DEBUG
        rl_close(rl_fd1);
        ok("file descriptor closed\n"); // DEBUG
    } else if (pid < 0) { 
        error("fork() failed\n");
    } else {
        /**
        char buffer[10] = "1234567890";
        ssize_t wr = rl_write(rl_fd1, buffer, 10);
        if (wr == -1) error("could not write file\n");
        ok("buffer = '%s'\n", buffer); // DEBUG
        **/
        struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 5};
        int return_value = rl_fcntl(rl_fd2, F_SETLKW, &lock);
        if (return_value == -1) error("could not set lock\n");

        char buffer[6];
        ssize_t re = rl_read(rl_fd2, buffer, 5);
        buffer[5] = '\0';
        if (re == -1) { error("could not read file\n"); } else ok("buffer = '%s'\n", buffer); // DEBUG
        rl_close(rl_fd2);
        ok("file descriptor closed\n"); // DEBUG
    }
    wait(NULL);
    return EXIT_SUCCESS;
}