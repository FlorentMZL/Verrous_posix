#include <stdlib.h>
#include <stdio.h>
#include <stdint.h> 
#include <string.h>
#include <errno.h>

#include <sys/types.h>          /* For types    */
#include <sys/stat.h>           /* For mode constants   */
#include <sys/mman.h>           /* For mmap()   */

#include <unistd.h>             /* For mode constants   */
#include <fcntl.h>              /* For O_* constants    */
#include <pthread.h>            /* For mutexes      */
#include <semaphore.h>          /* For semaphores   */

/**
 * #DEFINE propres à la librairie
*/
#define BOOLEAN uint8_t
#define FALSE 0
#define TRUE !FALSE

#define DEBUG 1
#define DEBUG_MEMORY 0

int memory_allocations = 0;

#define debug(...)                                                                  \
    if (DEBUG) {                                                                    \
        printf("\033[0;33m[DBG #%d] @ '%s' (%d)\t> ", getpid(), __func__, __LINE__); \
        printf(__VA_ARGS__);                                                        \
        printf("\n\033[0;37m");                                                     \
    }                                                                               

#define debug_memory(...)                                                           \
    if (DEBUG_MEMORY) {                                                             \
        printf("\033[0;33m[DBG #%d] @ '%s' (%d)\t> ", getpid(), __func__, __LINE__); \
        printf(__VA_ARGS__);                                                        \
        printf("\n\033[0;37m");                                                     \
    }

#define info(...)                                                                   \
    {                                                                               \
        printf("\033[0;32m[INF #%d] @ '%s' (%d)\t> ", getpid(), __func__, __LINE__); \
        printf(__VA_ARGS__);                                                        \
        printf("\n\033[0;37m");                                                     \
    }                          

#define error(...)                                                                  \
    {                                                                               \
        printf("\033[0;31m[ERR #%d] @ '%s' (%d)\t> ", getpid(), __func__, __LINE__); \
        printf(__VA_ARGS__);                                                        \
        printf(" ERRNO = %d\n\033[0;37m", errno);                                   \
    }

#define malloc(...)                                                                 \
    ({                                                                              \
        void *ptr = malloc(__VA_ARGS__);                                            \
        debug_memory("[%02d malloc() = %p]\n", memory_allocations + 1, ptr);        \
        if (ptr == NULL) {                                                          \
            error("malloc() failed");                                               \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
        memory_allocations++;                                                       \
        ptr;                                                                        \
    })

#define free(...)                                                                   \
    ({                                                                              \
        debug_memory("[%02d free() = %p", memory_allocations, __VA_ARGS__);         \
        free(__VA_ARGS__);                                                          \
        memory_allocations--;                                                       \
        if (DEBUG_MEMORY) {                                                         \
            if (memory_allocations == 0) {                                          \
                printf("\033[0;33m | all memory deallocated]\n");                   \
            }                                                                       \
            else {                                                                  \
                printf("\033[0;33m]\n");                                            \
            }                                                                       \
        }                                                                           \
        printf("\033[0;37m");                                                       \
    })

/**
 * !!!
 *    SUSCEPTIBLE (AVEC GRANDE PROBABILITE) DE CHANGER
 *    JE NE SAIS PAS QUELLES VALEURS METTRE
 * !!!
*/
#define NB_OWNRS 16    
#define NB_LOCKS 32
#define NB_FILES 256

// Potentiellement à modifier, valeur de l'énoncé
#define DEV_INO_MAX_SIZE 24

// Defines propres à la librairie

typedef struct {
    // Le thread qui possède le lock
    pid_t thread_id;
    // Le file descriptor du fichier (le "physique" ou le SMO?)
    int file_descriptor;
} rl_lock_owner;

typedef struct {
    int next_lock;
    // On verra plus tard
    off_t starting_offset;
    off_t length;
    short type; /* F_RDLCK F_WRLCK */
    size_t owners_count;
    rl_lock_owner lock_owners[NB_OWNRS];
} rl_lock;

typedef struct {
    // Devrait être à -1 par défaut? (-1 = dernier élément)
    int first_lock;
    // Tableau des verrous - OK (?)
    rl_lock lock_table[NB_LOCKS];
} rl_open_file;

typedef struct {
    // Le descripteur du fichier "physique"
    int file_descriptor;
    rl_open_file* rl_file;
} rl_descriptor;

/**
 * Ici, si j'ai bien compris, le but est bien d'ouvrir, sans ajouter de verrous ou quoi que ce soit.
*/
rl_descriptor rl_open(const char*, int, mode_t);

/**
 * Equivalent de close()
*/
int rl_close(rl_descriptor);

/**
 * La fonction qui gère les verrous
*/
int rl_fcntl(rl_descriptor, int, struct flock*);

/**
 * La fonction qui duplique un descripteur - (2)
*/
rl_descriptor rl_dup2(rl_descriptor, int);

/**
 * La fonction qui duplique un descripteur
*/
rl_descriptor rl_dup(rl_descriptor);

/**
 * La fonction qui crée un processus fils en copiant les verrous
*/
pid_t rl_fork();

/**
 * La fonction qui écrit dans un fichier
*/
ssize_t rl_write(rl_descriptor, const void*, size_t);

/**
 * La fonction qui lit dans un fichier
*/
ssize_t rl_read(rl_descriptor, void*, size_t);