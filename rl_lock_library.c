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
    char* env_prefix = getenv("RL_LOCK_PREFIX\n");
    if (env_prefix == NULL) { env_prefix = malloc(sizeof(char) * 2); env_prefix[0] = 'f'; env_prefix[1] = '\0'; } // Valeur par défaut
    debug("prefix from environment = '%s'\n", env_prefix);	// DEBUG
    char* prefix = malloc(sizeof(char) * (1 + strlen(env_prefix) + 1));
    strcpy(prefix, "/\n");
    strcat(prefix, env_prefix);
    // Construction du path du SMO 
    char* fd_dev = malloc(sizeof(char) * max_size); char* fd_ino = malloc(sizeof(char) * max_size);
    sprintf(fd_dev, "%ld", fstats->st_dev); sprintf(fd_ino, "%ld", fstats->st_ino);
    char* smo_path = malloc(sizeof(char) * (1 + strlen(prefix) + 1 + strlen(fd_dev) + 1 + strlen(fd_ino) + 1));
    strcpy(smo_path, prefix); strcat(smo_path, "_\n"); strcat(smo_path, fd_dev); strcat(smo_path, "_\n"); strcat(smo_path, fd_ino);
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
    info("constructing shared memory object path\n");
    // Récupération des informations du fichier
    struct stat fstats;
    info("getting file stats\n");
    char* smo_path = rl_path(fd, &fstats, DEV_INO_MAX_SIZE);
    debug("file descriptor dev block = %ld, inode = %ld", fstats.st_dev, fstats.st_ino);	// DEBUG
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
            // debug("handling lock n°%ld, \n", i);	// DEBUG\n");
            rl_lock lock;
            // Valeurs par défaut
            lock.next_lock = -2;
            /** TODO: A vérifier/modifier */
            {
                lock.starting_offset = -1;
                lock.length = -1;
                lock.type = -1;
                lock.owners_count = 0;
                // debug("starting_offset = %ld, length = %ld, type = %d, owners_count = %ld\n", lock.starting_offset, lock.length, lock.type, lock.owners_count);	// DEBUG
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
                case F_RDLCK:
                    info("requesting a read lock\n");   
                    print_flock(lock);	// DEBUG                 
                    // F_RDLCK: on vérifie si le fichier est déjà locké en écriture
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset != -1 && current_lock.type == F_WRLCK) {
                            // On vérifie si le lock est compatible avec le lock en écriture
                            if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len) {
                                // On vérifie si le lock est déjà pris par le thread courant
                                for (size_t j = 0; j < current_lock.owners_count; j++) {
                                    rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                                    if (current_lock_owner.thread_id == lock_owner.thread_id && current_lock_owner.file_descriptor == lock_owner.file_descriptor) {
                                        // Il existe déjà un lock d'écriture qui appartient au thread courant, on doit atomiquement le remplacer par un lock de lecture
                                        debug("demoting owner's write lock to read lock\n");	// DEBUG
                                        current_lock.type = F_RDLCK;
                                        return 0;
                                    }
                                }
                                // Le lock n'est pas déjà pris par le thread courant
                                // On ajoute le lock owner à la liste des lock owners
                                current_lock.lock_owners[current_lock.owners_count] = lock_owner;
                                current_lock.owners_count += 1;
                                info("read lock granted\n");	// DEBUG
                                return 0;
                            }
                        }
                    }
                    // Si on arrive ici, c'est que le fichier n'est pas locké en écriture
                    // On vérifie si le fichier est déjà locké en lecture
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset != -1 && current_lock.type == F_RDLCK) {
                            // On vérifie si le lock est compatible avec le lock en lecture
                            if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len) {
                                // On vérifie si le lock est déjà pris par le thread courant
                                for (size_t j = 0; j < current_lock.owners_count; j++) {
                                    rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                                    if (current_lock_owner.thread_id == lock_owner.thread_id && current_lock_owner.file_descriptor == lock_owner.file_descriptor) {
                                        // Le lock est déjà pris par le thread courant
                                        debug("read lock already taken by owner\n");	// DEBUG
                                        return 0;
                                    }
                                }
                                // Le lock n'est pas déjà pris par le thread courant
                                // On ajoute le lock owner à la liste des lock owners
                                current_lock.lock_owners[current_lock.owners_count] = lock_owner;
                                current_lock.owners_count += 1;
                                info("read lock granted\n");	// DEBUG
                                return 0;
                            }
                        }
                    }
                    // Si on arrive ici, c'est que le fichier n'est pas locké en lecture
                    // On cherche un lock libre
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset == -1) {
                            // On ajoute le lock
                            current_lock.starting_offset = lock->l_start;
                            current_lock.length = lock->l_len;
                            current_lock.type = F_RDLCK;
                            current_lock.next_lock = -2;
                            // On ajoute le lock owner à la liste des lock owners
                            current_lock.lock_owners[0] = lock_owner;
                            current_lock.owners_count = 1;
                            info("read lock granted\n");	// DEBUG
                            return 0;
                        }
                    }
                    // Si on arrive ici, c'est qu'il n'y a pas de lock libre
                    errno = ENOLCK;
                    error("no free locks\n");	// DEBUG
                    return -1;
                case F_WRLCK:
                    info("requesting a write lock\n");	// DEBUG
                    print_flock(lock);	// DEBUG
                    // F_WRLCK: on vérifie si le fichier est déjà locké en écriture
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset != -1 && current_lock.type == F_WRLCK) {
                            // On vérifie si le lock est compatible avec le lock en écriture
                            if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len) {
                                // On vérifie si le lock est déjà pris par le thread courant
                                for (size_t j = 0; j < current_lock.owners_count; j++) {
                                    rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                                    if (current_lock_owner.thread_id == lock_owner.thread_id && current_lock_owner.file_descriptor == lock_owner.file_descriptor) {
                                        // Le lock est déjà pris par le thread courant
                                        // On vérifie que c'est bien la même région qui est lockée
                                        if (current_lock.starting_offset == lock->l_start && current_lock.length == lock->l_len) {
                                            debug("write lock already taken by owner\n");	// DEBUG
                                            return 0;
                                        }
                                    }
                                }
                                // Le lock n'est pas déjà pris par le thread courant
                                // On ajoute le lock owner à la liste des lock owners
                                current_lock.lock_owners[current_lock.owners_count] = lock_owner;
                                current_lock.owners_count += 1;
                                info("write lock granted\n");	// DEBUG
                                return 0;
                            }
                        }
                    }
                    // Si on arrive ici, c'est que le fichier n'est pas locké en écriture
                    // On vérifie si le fichier est déjà locké en lecture
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset != -1 && current_lock.type == F_RDLCK) {
                            rl_lock current_lock = descriptor.rl_file->lock_table[i];
                            // On vérifie si le lock est compatible avec le lock en lecture
                            if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len) {
                                // On vérifie si le lock est déjà pris par le thread courant
                                for (size_t j = 0; j < current_lock.owners_count; j++) {
                                    rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                                    if (current_lock_owner.thread_id == lock_owner.thread_id && current_lock_owner.file_descriptor == lock_owner.file_descriptor) {
                                        if (current_lock.owners_count == 1) {
                                            // Le lock n'est pris que par le thread courant, on le promeut en écriture
                                            current_lock.type = F_WRLCK;
                                            debug("promoting owner's read lock to a write lock\n");	// DEBUG
                                            return 0;
                                        } else {
                                            // Il y a d'autres lock owners, on ne peut pas promouvoir le lock                                            
                                            errno = EAGAIN;
                                            error("could not ppromote owner's read lock to a write lock because there are other owners\n");	// DEBUG
                                            return -1;
                                        }
                                    }
                                }
                                // Le lock n'est pas déjà pris par le thread courant
                                // On ajoute le lock owner à la liste des lock owners
                                current_lock.lock_owners[current_lock.owners_count] = lock_owner;
                                current_lock.owners_count += 1;
                                info("write lock granted\n");	// DEBUG
                                return 0;
                            }
                        }
                    }
                    // Si on arrive ici, c'est que le fichier n'est pas locké en lecture ni en écriture
                    // On cherche un lock libre
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset == -1) {
                            // On ajoute le lock
                            current_lock.starting_offset = lock->l_start;
                            current_lock.length = lock->l_len;
                            current_lock.type = F_WRLCK;
                            current_lock.next_lock = -2;
                            // On ajoute le lock owner à la liste des lock owners
                            current_lock.lock_owners[0] = lock_owner;
                            current_lock.owners_count = 1;
                            info("write lock granted\n");	// DEBUG
                            return 0;
                        }
                    }
                    // Si on arrive ici, c'est qu'il n'y a pas de lock libre
                    errno = ENOLCK;
                    error("no free locks available\n");
                    return -1;
                case F_UNLCK: /** todo vérifier que ça marche */
                    info("requesting an unlock\n");	// DEBUG
                    print_flock(lock);	// DEBUG
                    // On cherche le lock dans la liste des lock owners
                    for (size_t i = 0; i < NB_LOCKS; i++) {
                        rl_lock current_lock = descriptor.rl_file->lock_table[i];
                        if (current_lock.starting_offset != -1) {
                            // On vérifie si le lock est compatible avec le lock en écriture
                            if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len) {
                                // On vérifie si le lock est déjà pris par le thread courant
                                for (size_t j = 0; j < current_lock.owners_count; j++) {
                                    rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                                    if (current_lock_owner.thread_id == lock_owner.thread_id && current_lock_owner.file_descriptor == lock_owner.file_descriptor) {
                                        // On supprime le lock owner de la liste des lock owners
                                        for (size_t k = j; k < current_lock.owners_count - 1; k++) {
                                            current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                                        }
                                        current_lock.owners_count -= 1;
                                        // Si le lock n'a plus de lock owners, on le supprime
                                        if (current_lock.owners_count == 0) {
                                            current_lock.starting_offset = -1;
                                            current_lock.length = -1;
                                            current_lock.type = -1;
                                            current_lock.next_lock = -1;
                                        }
                                        info("unlock granted\n");	// DEBUG
                                        return 0;
                                    }
                                }
                            }
                        }
                    }
                    // On regarde si le lock est au milieu d'un autre lock appartenant au thread courant
                    
                    // Si on arrive ici, c'est que le lock n'est pas pris par le thread courant
                    error("owner not found\n");
                    return -1;  
            }
            break;
        case F_SETLKW: /** TODO: implémenter (extension) */
            break;
        case F_GETLK: /** TODO: implémenter? (pas nécessaire selon le sujet ) */
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
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        const size_t owners_count = current_lock.owners_count;
        for (size_t j = 0; j < owners_count; j++) {
            rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
            if (current_lock_owner.thread_id == getpid()) {
                debug("found lock owner at index %ld\n", j);	// DEBUG
                // On ajoute le nouveau lock owner
                current_lock.lock_owners[owners_count] = new_owner;
                current_lock.owners_count += 1;
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
    return rl_dup2(descriptor, dup(descriptor.file_descriptor));
}

pid_t rl_fork() {
    pid_t pid = fork();
    if (pid == 0) {
        // On est dans le fils
        pid_t parent = getppid();
        // On parcourt la liste des fichiers ouverts
        for (size_t i = 0; i < rl_all_files.files_count; i++) {
            // Si on trouve le parent dans la liste des lock owners du fichier courant (rl_all_files.files[i]) alors on ajoute le fils à la liste des lock owners
            rl_open_file* current_file = rl_all_files.open_files[i];
            for (size_t j = 0; j < NB_LOCKS; j++) {
                const size_t owners_count = current_file->lock_table[j].owners_count;
                for (size_t k = 0; k < owners_count; k++) {
                    if (current_file->lock_table[j].lock_owners[k].thread_id == parent) {
                        // On ajoute le nouveau lock owner
                        rl_lock_owner new_owner = { .thread_id = getpid(), .file_descriptor = current_file->lock_table[j].lock_owners[k].file_descriptor };
                        current_file->lock_table[j].lock_owners[owners_count] = new_owner;
                        current_file->lock_table[j].owners_count += 1;
                        info("added child process to lock owners\n");
                        break;
                    }
                }
            }
        }
    } else if (pid < 0) {
        error("fork() failed\n");
    } 
    return pid;
}

ssize_t rl_write(rl_descriptor descriptor, const void* buffer, size_t count) {
    // On parcourt la liste des locks du fichier
    for (size_t i = 0; i < NB_LOCKS; i++) {
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        // Si le lock est de type F_WRLCK et que le thread courant est dans la liste des lock owners, alors on peut écrire
        if (current_lock.type == F_WRLCK) {
            const size_t owners_count = current_lock.owners_count;
            for (size_t j = 0; j < owners_count; j++) {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid()) {
                    // On vérifie que la position du curseur est bien dans l'intervalle
                    if (lseek(descriptor.file_descriptor, 0, SEEK_CUR) < current_lock.starting_offset || lseek(descriptor.file_descriptor, 0, SEEK_CUR) > current_lock.starting_offset + current_lock.length) {
                        errno = EACCES;
                        return -1;
                    }
                    // On vérifie que la taille du buffer est bien dans l'intervalle
                    if (count < 0 || count > current_lock.length) {
                        errno = EACCES;
                        return -1;
                    }
                    // On déplace le curseur à la position starting_offset
                    lseek(descriptor.file_descriptor, current_lock.starting_offset, SEEK_SET);
                    // On écrit
                    return write(descriptor.file_descriptor, buffer, count);
                }                    
            }
        }
    }
    // Sinon on ne peut pas écrire
    errno = EACCES;
    return -1;
}

ssize_t rl_read(rl_descriptor descriptor, void* buffer, size_t count) {
    // On parcourt la liste des locks du fichier
    for (size_t i = 0; i < NB_LOCKS; i++) {
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        // Si le lock est de type F_RDLCK et que le thread courant est dans la liste des lock owners, alors on peut lire
        if (current_lock.type == F_RDLCK) {
            const size_t owners_count = current_lock.owners_count;
            for (size_t j = 0; j < owners_count; j++) {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid()) {
                    // On vérifie que la position du curseur est bien dans l'intervalle
                    if (lseek(descriptor.file_descriptor, 0, SEEK_CUR) < current_lock.starting_offset || lseek(descriptor.file_descriptor, 0, SEEK_CUR) > current_lock.starting_offset + current_lock.length) {
                        errno = EACCES;
                        return -1;
                    }
                    // On vérifie que la taille du buffer est bien dans l'intervalle
                    if (count < 0 || count > current_lock.length) {
                        errno = EACCES;
                        return -1;
                    }
                    // On déplace le curseur à la position starting_offset
                    lseek(descriptor.file_descriptor, current_lock.starting_offset, SEEK_SET);
                    // On lit
                    return read(descriptor.file_descriptor, buffer, count);
                }
            }
        }
    }
    // Sinon on ne peut pas lire
    errno = EACCES;
    return -1;
}

int main(int argc, char** argv) {
    char* file_name;
    if (argc < 2) file_name = "test.txt"; else file_name = argv[1];
    info("file_name from args = '%s'\n", file_name);	// DEBUG

    rl_descriptor rl_fd1 = rl_open(file_name, O_RDWR, S_IRUSR | S_IWUSR);
    debug("file descriptor = %d\n", rl_fd1.file_descriptor);	// DEBUG

    /*
        // On duplique le descripteur de fichier
        rl_descriptor rl_fd2 = rl_dup(rl_fd1);
        debug("file descriptor = %d\n", rl_fd2.file_descriptor);	// DEBUG

        // On pose un verrou en lecture sur le fichier
        struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10 };
        rl_fcntl(rl_fd1, F_SETLK, &lock);
        debug("lock set\n");	// DEBUG
    */

    // On fork
    struct flock lock = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10};
    int return_value = rl_fcntl(rl_fd1, F_SETLK, &lock);
    if (return_value == -1) error("could not set lock\n");
    struct flock lock2 = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 4, .l_len = 2};
    return_value = rl_fcntl(rl_fd1, F_SETLK, &lock2);
    pid_t pid = rl_fork();
    if (pid == 0) {
        // On pose un verrou en ecriture sur le fichier
        debug("lock set\n"); // DEBUG
        // On lit le fichier
        char buffer[6];
        ssize_t re = rl_read(rl_fd1, buffer, 5);
        buffer[5] = '\0';
        if (re == -1) {
            error("could not read file\n");
        }
        debug("buffer = '%s'\n", buffer); // DEBUG
        // On ferme le descripteur de fichier
        rl_close(rl_fd1);
        debug("file descriptor closed\n"); // DEBUG
    }
    else if (pid < 0) {
        error("fork() failed\n");
    } else {
        char buffer[10] = "1234567890";
        ssize_t wr = rl_write(rl_fd1, buffer, 10);
        if (wr == -1) {
            error("could not write file\n");
        }
        debug("buffer = '%s'\n", buffer); // DEBUG
    }
    info("unlinking shared memory object\n");
    char *smo_path = rl_path(rl_fd1.file_descriptor, NULL, 24);
    shm_unlink(smo_path);
    // TODO
    info("closing first file descriptor\n"); // DEBUG
    rl_close(rl_fd1);
    free(smo_path);
    return 0;
}