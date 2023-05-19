/**
 * JEDDI SKANDER - 21957008
 * MAZELET FLORENT - XXXXXXXX (TODO: A COMPLETER)
*/

#include "rl_lock_library.h"

static struct {
    int files_count;
    rl_open_file* open_files[NB_FILES];
} rl_all_files;

static char* rl_path(int fd, struct stat* fstats, size_t max_size) {
    // Récupération du path du SMO
    BOOLEAN delete_fstats = FALSE;
    if (!fstats) { fstats = malloc(sizeof(struct stat)); delete_fstats = TRUE; }
    fstat(fd, fstats);
    char* env_prefix = getenv("RL_LOCK_PREFIX");
    if (env_prefix == NULL) { env_prefix = malloc(sizeof(char) * 2); env_prefix[0] = 'f'; env_prefix[1] = '\0'; } // Valeur par défaut
    debug("prefix from environment = '%s'\n", env_prefix);	// DEBUG
    char* prefix = malloc(sizeof(char) * (1 + strlen(env_prefix) + 1));
    strcpy(prefix, "/");
    strcat(prefix, env_prefix);
    // Construction du path du SMO 
    char* fd_dev = malloc(sizeof(char) * max_size); char* fd_ino = malloc(sizeof(char) * max_size);
    sprintf(fd_dev, "%ld", fstats->st_dev); sprintf(fd_ino, "%ld", fstats->st_ino);
    char* smo_path = malloc(sizeof(char) * (1 + strlen(prefix) + 1 + strlen(fd_dev) + 1 + strlen(fd_ino) + 1));
    strcpy(smo_path, prefix); strcat(smo_path, "_"); strcat(smo_path, fd_dev); strcat(smo_path, "_"); strcat(smo_path, fd_ino);
    // Libération de la mémoire
    free(fd_dev); free(fd_ino); free(prefix); free(env_prefix);
    if (delete_fstats) free(fstats);
    return smo_path;
}

/**
 * Terminologie:
 *      - "physique" = le fichier physique
 *      - "SMO" = shared memory object
 * Etapes:
 * 1. Ouvrir le fichier "physique" avec open() - si le fichier n'existe pas, mettre rl_descriptor.file_descriptor à -1 et retourner
 * 2. Récupérer les informations du fichier avec fstat()
 * 3. Construire le path du SMO avec getenv() et les informations du fichier
 * 4. Ouvrir le SMO avec shm_open()
 *      + Si le SMO n'existe pas, il sera créé automatiquement MAIS il faut ftruncate()
 * 5. Map le SMO en mémoire avec mmap()
 * 6. Créer le lock avec les informations du fichier
 *     + Si le lock_id est -1, c'est le premier lock donc next_lock = 0
 *     + Sinon, next_lock = rl_file->first_lock
 * 7. Ajouter le lock au tableau de locks du fichier
 * 8. Ajouter le fichier au tableau de fichiers ouverts
 * 9. Retourner le rl_descriptor
*/
rl_descriptor rl_open(const char* path, int flags, mode_t mode) {
    // Ouverture "physique" du fichier
    info("opening file on disk\n");
    int fd = open(path, flags, mode);
    rl_descriptor rl_fd = { -1, NULL };
    if (fd < 0) { // Le fichier physique existe
        error("couldn't open file on disk\n");
        close(fd);
        return rl_fd;
    }
    debug("file descriptor = %d\n", fd);	// DEBUG
    info("constructing shared memory object path\n")
    // Récupération des informations du fichier
    struct stat fstats;
    info("getting file stats\n");
    char* smo_path = rl_path(fd, &fstats, DEV_INO_MAX_SIZE);
    debug("file descriptor dev block = %ld, inode = %ld\n", fstats.st_dev, fstats.st_ino);	// DEBUG
    // Récupération du prefixe du SMO
    debug("final shared memory object path = '%s'\n", smo_path);	// DEBUG
    // Ouverture du SMO
    info("opening and/or creating shared memory object\n");
    int smo_fd = shm_open(smo_path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    BOOLEAN smo_was_on_disk = FALSE;
    if (smo_fd == -1 && errno == EEXIST) {
        // Le SMO existe déjà, on l'ouvre de nouveau sans O_CREAT
        smo_fd = shm_open(smo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if (smo_fd == -1) {
            error("couldn't open shared memory object\n");
            close(smo_fd);
            free(smo_path);
            return rl_fd;
        }
        debug("shared memory object exists, opened with fd = %d\n", smo_fd);	// DEBUG
        smo_was_on_disk = TRUE;
    } else if (smo_fd == -1) { // Le SMO n'existe pas et on n'a pas pu le créer
        error("couldn't create shared memory object\n");
        close(smo_fd);
        free(smo_path);
        return rl_fd; // On retourne une structure vide avec un file_descriptor à -1 et un rl_file à NULL pour indiquer une erreur
    }
    if (!smo_was_on_disk) { // Le SMO n'existait pas, on le crée et on le tronque à la taille de la structure rl_open_file
        debug("shared memory object doesn't exist, creating & truncating to %ld\n", sizeof(rl_open_file));	// DEBUG
        ftruncate(smo_fd, sizeof(rl_open_file));
    }
    info("mapping shared memory object in memory\n");
    // Map le fichier en mémoire - là, on a un pointeur sur la structure rl_open_file qui est mappée en mémoire à travers le SMO
    void* mmap_ptr = mmap(NULL, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, smo_fd, 0);
    if (mmap_ptr == (void*) MAP_FAILED) {
        error("couldn't map shared memory object in memory\n");
        rl_fd.file_descriptor = -1;
        rl_fd.rl_file = NULL;
        free(smo_path);
        return rl_fd;
    }
    rl_open_file* rl_mapped_file = (rl_open_file*) mmap_ptr;
    debug("address of mapped file = %p\n", (void*) rl_mapped_file);	// DEBUG
    if (!smo_was_on_disk) { // Si le SMO n'existe pas, remplir les valeurs par défaut de rl_open_file (énoncé)
        debug("shared memory object didn't exist, filling with default values\n");	// DEBUG
        if (rl_mapped_file) debug("mapped file is ok\n");    // DEBUG
        rl_mapped_file->first_lock = -2; // (pas sûr)
        debug("first lock index set to %d\n", rl_mapped_file->first_lock);	// DEBUG
        for (size_t i = 0; i < NB_LOCKS; i++) {
            // debug("handling lock n°%ld, ", i);	// DEBUG");
            rl_lock lock;
            // Valeurs par défaut
            lock.next_lock = -2;
            /** TODO: A vérifier/modifier */
            {
                lock.starting_offset = -1;
                lock.length = -1;
                lock.type = -1;
                lock.owners_count = 0;
                debug("starting_offset = %ld, length = %ld, type = %d, owners_count = %ld\n", lock.starting_offset, lock.length, lock.type, lock.owners_count);	// DEBUG
            }
            rl_mapped_file->lock_table[i] = lock;
        }
    }
    /** TODO: On met à jour la structure de retour - mais quel descripteur? celui du fichier ou celui du SMO? */
    rl_fd.file_descriptor = fd; // ou smo_fd?
    rl_fd.rl_file = rl_mapped_file;
    info("shared memory object mapped in memory\n");	// DEBUG
    // On met à jour la structure qui contient tous les rl_open_file(s) - on vérifie avant si le fichier n'est pas déjà ouvert
    info("checking if file is already opened\n");
    for (size_t i = 0; i < rl_all_files.files_count; i++) {
        /** TODO: Est ce qu'on doit vérifier l'adresse de la structure ou juste le pointeur vers la structure? Ou même vérifier ça tout court? */
        debug("&rl_all_files.open_files[i] = %p vs %p = rl_mapped_file\n", &rl_all_files.open_files[i], &rl_mapped_file);	// DEBUG
        if (&rl_all_files.open_files[i] == &rl_mapped_file) {
            info("yes, returning\n");
            free(smo_path);
            return rl_fd;
        }
    }// Si on arrive ici, c'est que le fichier n'est pas déjà ouvert
    info("no, appending to opened files\n");
    rl_all_files.open_files[rl_all_files.files_count] = rl_mapped_file;
    rl_all_files.files_count += 1;
    // Libération de la mémoire
    free(smo_path);
    return rl_fd;
}

int rl_close(rl_descriptor rl_fd) {
    int ret = close(rl_fd.file_descriptor);
    info("file descriptor closed\n");
    // On récupère le lock owner courant
    rl_lock_owner current_lock_owner = { .thread_id = getpid(), .file_descriptor = rl_fd.file_descriptor };
    info("removing lock owner & possibly lock\n");
    int lock_index = -1;
    for (size_t i = 0; i < NB_LOCKS; i++) {
        const size_t owners_count = rl_fd.rl_file->lock_table[i].owners_count;
        // On cherche le lock owner dans la liste des lock owners
        for (size_t j = 0; j < owners_count; j++) {
            if (rl_fd.rl_file->lock_table[i].lock_owners[j].thread_id == current_lock_owner.thread_id && rl_fd.rl_file->lock_table[i].lock_owners[j].file_descriptor == current_lock_owner.file_descriptor) {
                debug("found lock owner at index %ld\n", j);	// DEBUG
                lock_index = j;
                /**
                 * On le supprime (en décalant tous les autres lock owners) - pas sûr que ça marche - àà priori oui
                */
                rl_fd.rl_file->lock_table[i].lock_owners[j] = rl_fd.rl_file->lock_table[i].lock_owners[owners_count - 1];
                rl_fd.rl_file->lock_table[i].owners_count -= 1;
                break;
            }
        }
    }
    if (lock_index != -1 && rl_fd.rl_file->lock_table[lock_index].owners_count == 0) { // Si le verrou n'a plus d'owner, on le supprime
        debug("no more owners, removing lock\n");	// DEBUG
        rl_fd.rl_file->lock_table[lock_index].starting_offset = -1;
        rl_fd.rl_file->lock_table[lock_index].length = -1;
        rl_fd.rl_file->lock_table[lock_index].type = -1;
        rl_fd.rl_file->lock_table[lock_index].next_lock = -2;
    }
    return ret;
}

int rl_fcntl(rl_descriptor descriptor, int command, struct flock* lock) {
    rl_lock_owner lock_owner = { .thread_id = getpid(), .file_descriptor = descriptor.file_descriptor };
    switch (command) {
        case F_SETLK: 
            // On recupère le type d'opération
            switch (lock->l_type) {
                case F_RDLCK: /** TODO: implémenter */
                    break;
                case F_WRLCK: /** TODO: implémenter */
                    break;
                case F_UNLCK: /** TODO: vérifier que ça marche */
                    // On parcourt la liste des locks du fichier pour trouver le lock qui correspond à l'owner
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        const size_t owners_count = descriptor.rl_file->lock_table[i].owners_count;
                        for (size_t j = 0; j < owners_count; j++) {
                            if (descriptor.rl_file->lock_table[i].lock_owners[j].thread_id == lock_owner.thread_id && descriptor.rl_file->lock_table[i].lock_owners[j].file_descriptor == lock_owner.file_descriptor) {
                                info("removing lock owner\n");
                                // On le supprime (en décalant tous les autres lock owners) - pas sûr que ça marche
                                descriptor.rl_file->lock_table[i].lock_owners[j] = descriptor.rl_file->lock_table[i].lock_owners[owners_count - 1];
                                descriptor.rl_file->lock_table[i].owners_count -= 1;
                                // Si le verrou n'a plus d'owner, on le supprime
                                if (descriptor.rl_file->lock_table[i].owners_count == 0) {
                                    debug("no more owners, removing lock\n");
                                    descriptor.rl_file->lock_table[i].starting_offset = -1;
                                    descriptor.rl_file->lock_table[i].length = -1;
                                    descriptor.rl_file->lock_table[i].type = -1;
                                    descriptor.rl_file->lock_table[i].next_lock = -2;
                                }
                            }
                        }
                    }
                    return 0;
            }
            break;
        case F_SETLKW: /** TODO: implémenter */
            break;
        case F_GETLK: /** TODO: implémenter */
            break;
    }
    return 0;
}

rl_descriptor rl_dup2(rl_descriptor descriptor, int new_file_descriptor) {
    // On duplique le descripteur de fichier
    rl_descriptor new_descriptor;
    // Nouveau lock owner
    rl_lock_owner new_owner = { .thread_id = getpid(), .file_descriptor = new_file_descriptor };
    // On cherche le lock owner dans la liste des lock owners
    for (size_t i = 0; i < NB_LOCKS; i++) {
        const size_t owners_count = descriptor.rl_file->lock_table[i].owners_count;
        for (size_t j = 0; j < owners_count; j++) {
            if (descriptor.rl_file->lock_table[i].lock_owners[j].thread_id == getpid()) {
                debug("found lock owner at index %ld\n", j);	// DEBUG
                // On ajoute le nouveau lock owner
                descriptor.rl_file->lock_table[i].lock_owners[owners_count] = new_owner;
                descriptor.rl_file->lock_table[i].owners_count += 1;
                debug("added new lock owner\n");	// DEBUG
                break;
            }
        }
    }
    // On retourne le nouveau descripteur de fichier
    new_descriptor.file_descriptor = new_file_descriptor;
    new_descriptor.rl_file = descriptor.rl_file;
    return new_descriptor;
}

rl_descriptor rl_dup(rl_descriptor descriptor) {
    info("calling rl_dup2() from rl_dup()\n");
    return rl_dup2(descriptor, dup(descriptor.file_descriptor));
}

pid_t rl_fork() {
    pid_t pid = fork();
    if (pid == 0) {
        pid_t parent = getppid();
        // On duplique les descripteurs de fichier
        rl_open_file open_files[rl_all_files.files_count];
        // On copie la liste des fichiers ouverts
        memcpy(open_files, rl_all_files.open_files, rl_all_files.files_count * sizeof(rl_open_file));
        for (size_t i = 0; i < rl_all_files.files_count; i++) {
            // On examine chaque verrou
            for (size_t j = 0; j < NB_LOCKS; j++) {
                const size_t owners_count = open_files[i].lock_table[j].owners_count;
                for (size_t k = 0; k < owners_count; k++) {
                    // On recupere le descripteur de fichier du lock owner
                    rl_lock_owner lock_owner = open_files[i].lock_table[j].lock_owners[k];
                    if (lock_owner.thread_id == parent) {
                        // On ajoute le nouveau lock owner
                        open_files[i].lock_table[j].lock_owners[owners_count] = (rl_lock_owner) { .thread_id = getpid(), .file_descriptor = lock_owner.file_descriptor };
                        open_files[i].lock_table[j].owners_count += 1;
                        debug("added new lock owner\n");	// DEBUG
                        break;
                    }
                }
            }
        }
        // On copie la liste des fichiers ouverts dans la liste globale
        memcpy(rl_all_files.open_files, open_files, rl_all_files.files_count * sizeof(rl_open_file));
    } else if (pid < 0) {
        error("fork() failed\n");
    } 
    return pid;
}

int main(int argc, char** argv) {
    char* file_name;
    if (argc < 2) file_name = "test.txt"; else file_name = argv[1];
    info("file_name from args = '%s'\n", file_name);	// DEBUG
    rl_descriptor rl_fd1 = rl_open(file_name, O_RDWR, S_IRUSR | S_IWUSR);
    debug("first file descriptor = %d\n", rl_fd1.file_descriptor);	// DEBUG
    rl_descriptor rl_fd2 = rl_dup(rl_fd1);
    debug("second file descriptor = %d\n", rl_fd2.file_descriptor);	// DEBUG
    info("unlinking shared memory object\n");
    char* smo_path = rl_path(rl_fd1.file_descriptor, NULL, 24);
    shm_unlink(smo_path);
    // TODO
    info("closing first file descriptor\n");	// DEBUG
    rl_close(rl_fd1);
    info("closing second file descriptor\n");	// DEBUG
    rl_close(rl_fd2);
    free(smo_path);
    return 0;
}