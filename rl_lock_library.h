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
    // Tableau des verrous - OK(?)
    rl_lock lock_table[NB_LOCKS];
} rl_open_file;

typedef struct {
    // Le descripteur du fichier "physique"
    int file_descriptor;
    rl_open_file *rl_file;
} rl_descriptor;

/**
 * Ici, si j'ai bien compris, le but est bien d'ouvrir, sans ajouter de verrous ou quoi que ce soit.
*/
rl_descriptor rl_open(const char*, int, mode_t);

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

