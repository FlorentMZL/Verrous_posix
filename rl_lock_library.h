#ifndef __RL_LOCK_LIBRARY_H__
#define __RL_LOCK_LIBRARY_H__

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/types.h> /* For types    */
#include <sys/stat.h>  /* For mode constants   */
#include <sys/mman.h>  /* For mmap()   */
#include <sys/wait.h>  /* For wait()   */

#include <unistd.h>    /* For mode constants   */
#include <fcntl.h>     /* For O_* constants    */
#include <pthread.h>   /* For mutexes      */
#include <semaphore.h> /* For semaphores   */
#include <signal.h>    /* For signals     */

/**
 * #DEFINE propres à la librairie
 */

#define BOOLEAN uint8_t
#define FALSE 0
#define TRUE !FALSE

#define LOG_LEVEL -1

extern int memory_allocations;

#define debug(...)                                                                          \
    if (LOG_LEVEL >= 3)                                                                     \
    {                                                                                       \
        printf("\033[0;33m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\033[0;37m");                                                               \
    }

#define debug_memory(...)                                                                   \
    if (LOG_LEVEL >= 4)                                                                     \
    {                                                                                       \
        printf("\033[0;33m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\033[0;37m");                                                               \
    }

#define info(...)                                                                           \
    if (LOG_LEVEL >= 1)                                                                     \
    {                                                                                       \
        printf("\033[0;34m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\033[0;37m");                                                               \
    }

#define ok(...)                                                                             \
    if (LOG_LEVEL >= 1)                                                                     \
    {                                                                                       \
        printf("\033[0;32m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\033[0;37m");                                                               \
    }

#define error(...)                                                                          \
    {                                                                                       \
        printf("\033[0;31m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\t%s (%d)\n\033[0;37m", strerror(errno), errno);                            \
    }

#define test(...)                                                                           \
    {                                                                                       \
        printf("\033[0;32m[#%d] %s @ %03d [%s]: ", getpid(), __FILE__, __LINE__, __func__); \
        printf(__VA_ARGS__);                                                                \
        printf("\033[0;37m");                                                               \
    }

#define malloc(...)                                                          \
    ({                                                                       \
        void *ptr = malloc(__VA_ARGS__);                                     \
        debug_memory("[%02d malloc() = %p]\n", memory_allocations + 1, ptr); \
        if (ptr == NULL)                                                     \
        {                                                                    \
            error("malloc() failed");                                        \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
        memory_allocations++;                                                \
        ptr;                                                                 \
    })

#define free(...)                                                           \
    ({                                                                      \
        debug_memory("[%02d free() = %p", memory_allocations, __VA_ARGS__); \
        free(__VA_ARGS__);                                                  \
        memory_allocations--;                                               \
        if (LOG_LEVEL >= 4)                                                 \
        {                                                                   \
            if (memory_allocations == 0)                                    \
            {                                                               \
                printf("\033[0;33m | all memory deallocated]\n");           \
            }                                                               \
            else                                                            \
            {                                                               \
                printf("\033[0;33m]\n");                                    \
            }                                                               \
        }                                                                   \
        printf("\033[0;37m");                                               \
    })

// Une macro pour afficher la fonction actuelle et le numéro de ligne (pour le debug)
#define trace(...)                                                                                         \
    if (LOG_LEVEL >= 2)                                                                                    \
    {                                                                                                      \
        printf("\033[0;35m[#%d] %s @ %03d [%s]: %s", getpid(), __FILE__, __LINE__, __func__, __VA_ARGS__); \
    }

// Une macro pour afficher les propriétés d'un lock
#define print_lock(lock)                                                                                          \
    {                                                                                                             \
        info("lock = [ offset = %ld, length = %ld, type = %d ]\n", lock.starting_offset, lock.length, lock.type); \
    }

// Une macro pour afficher les propriétés d'un flock*
#define print_flock(flock)                                                                                       \
    {                                                                                                            \
        info("lock = [ offset = %ld, length = %ld, type = %d ]\n", flock->l_start, flock->l_len, flock->l_type); \
    }

#define pthread_mutex_lock(lock)            \
    {                                       \
        info("locking mutex @ %p\n", lock); \
        pthread_mutex_lock(lock);           \
    }

#define pthread_mutex_unlock(lock)            \
    {                                         \
        info("unlocking mutex @ %p\n", lock); \
        pthread_mutex_unlock(lock);           \
    }

#define pthread_cond_broadcast(cond)            \
    {                                           \
        info("broadcasting cond @ %p\n", cond); \
        pthread_cond_broadcast(cond);             \
    }

#define pthread_cond_wait(cond, mutex)                        \
    {                                                         \
        info("waiting on cond %p @ mutex %p\n", cond, mutex); \
        pthread_cond_wait(cond, mutex);                       \
    }

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

typedef struct
{
    // Le thread qui possède le lock
    pid_t thread_id;
    // Le file descriptor du fichier (le "physique" ou le SMO?)
    int file_descriptor;
} rl_lock_owner;

typedef struct
{
    unsigned int readers;
    unsigned int writers;
    // Le cond
    pthread_cond_t cond;
    int next_lock;
    // On verra plus tard
    off_t starting_offset;
    off_t length;
    short type; /* F_RDLCK F_WRLCK */
    size_t owners_count;
    rl_lock_owner lock_owners[NB_OWNRS];
} rl_lock;

typedef struct
{
    // Le mutex
    pthread_mutex_t mutex;
    // Le descripteur du SMO
    int smo_fd;
    // Devrait être à -1 par défaut? (-1 = dernier élément) : NON, -2 si il n'y a pas de verrou.
    int first_lock;
    // Tableau des verrous - OK (?)
    rl_lock lock_table[NB_LOCKS];
    // Nombre de fois ou le même fichier a été ovuert
    int open_instances;
} rl_open_file;

typedef struct
{
    // Le descripteur du fichier "physique"
    int file_descriptor;
    rl_open_file *rl_file;
} rl_descriptor;

/**
 * Ici, si j'ai bien compris, le but est bien d'ouvrir, sans ajouter de verrous ou quoi que ce soit.
 */
rl_descriptor rl_open(const char *, int, mode_t);

/**
 * Equivalent de close()
 */
int rl_close(rl_descriptor);

/**
 * La fonction qui gère les verrous
 */
int rl_fcntl(rl_descriptor, int, struct flock *);

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
ssize_t rl_write(rl_descriptor, const void *, size_t);

/**
 * La fonction qui lit dans un fichier
 */
ssize_t rl_read(rl_descriptor, void *, size_t);

/**
 * La fonction qui initialise la librairie
 */
int rl_init_library();

/**
 * Famille exec
 */
int rl_execl(const char *, const char *, ...);

#endif