/**
 * JEDDI SKANDER - 21957008
 * MAZELET FLORENT - XXXXXXXX (TODO: A COMPLETER)
 */
// TODO trier la liste des rl_lock par ordre croissant de starting_offset
#include "rl_lock_library.h"

int memory_allocations = 0;

/**
 * Signal handler
 */
void notify(int sig)
{
    return;
}
/**
 * La fonction qui initialise un mutex
 */
static int rl_mutex_init(pthread_mutex_t *pmutex)
{
    pthread_mutexattr_t mutexattr;
    int code; // Code de retour
    // Initialisation du mutex
    if ((code = pthread_mutexattr_init(&mutexattr)) != 0)
        return code; // Si erreur, on retourne le code d'erreur
    // On rend le mutex partagé entre les processus
    if ((code = pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED)) != 0)
        return code; // Si erreur, on retourne le code d'erreur
    // On initialise le mutex
    code = pthread_mutex_init(pmutex, &mutexattr);
    return code;
}

/**
 * La fonction qui initialise une condition
 */
static int rl_cond_init(pthread_cond_t *pcond)
{
    pthread_condattr_t condattr;
    int code; // Code de retour
    // Initialisation de la condition
    if ((code = pthread_condattr_init(&condattr)) != 0)
        return code; // Si erreur, on retourne le code d'erreur
    // On rend la condition partagée entre les processus
    if ((code = pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED)) != 0)
        return code; // Si erreur, on retourne le code d'erreur
    // On initialise la condition
    code = pthread_cond_init(pcond, &condattr);
    return code;
}

/**
 * La fonction qui verifie si un verrou a encore un owner ou pas
 */
static int rl_lock_check(rl_lock *lock)
{
    // Retourne l'indice du prochain lock si jamais il y a suppression du lock courant
    int nxt = lock->next_lock;
    debug("next lock index = %d\n", nxt);
    if (lock->owners_count == 0)
    {
        debug("lock owners = 0\n");
        // On reset le lock
        lock->readers = 0;
        lock->writers = 0;
        pthread_cond_broadcast(&lock->cond);
        lock->starting_offset = -1;
        lock->length = -1;
        lock->type = -1;
        lock->next_lock = -2;

        return nxt;
    }
    else
    {

        return -3;
    }
}

/**
 * la fonction pour ajouter un verrou
 */
static int rl_add_lock(rl_lock *lock, int id_back, int id_front, off_t starting_offset, off_t length, short type, pid_t thread_id, rl_descriptor *rl_fd, rl_lock_owner owner)
{
    for (size_t i = 0; i < NB_LOCKS; i++)
    {
        debug("adding lock [ next = %d | previous = %d ]\n", id_front, id_back);
        if (rl_fd->rl_file->lock_table[i].next_lock == -2)
        {
            rl_fd->rl_file->lock_table[i].lock_owners[0] = owner;
            rl_fd->rl_file->lock_table[i].starting_offset = starting_offset;
            rl_fd->rl_file->lock_table[i].length = length;
            rl_fd->rl_file->lock_table[i].type = type;
            rl_fd->rl_file->lock_table[i].owners_count = 1;

            rl_fd->rl_file->lock_table[i].next_lock = id_front;
            if (id_back != -2)
                rl_fd->rl_file->lock_table[id_back].next_lock = i;
            else
                rl_fd->rl_file->first_lock = i;
            switch (type)
            {
            case F_RDLCK:
                ok("read lock granted [ start = %ld | length = %ld ]\n", starting_offset, length);
                rl_fd->rl_file->lock_table[i].readers += 1;
                break;
            case F_WRLCK:
                ok("write lock granted [ start = %ld | length = %ld ]\n", starting_offset, length);
                rl_fd->rl_file->lock_table[i].writers += 1;
                break;
            }
            pthread_cond_broadcast(&lock->cond);
            pthread_mutex_unlock(&(rl_fd->rl_file->mutex));
            return 0;
        }
    }
    errno = ENOLCK;
    error("no more locks available\n");
    pthread_cond_broadcast(&lock->cond);
    pthread_mutex_unlock(&(rl_fd->rl_file->mutex));
    return -1;
}

static struct
{
    int files_count;
    rl_open_file *open_files[NB_FILES];
} rl_all_files;

static int rl_path(char *smo_path, int fd, struct stat *fstats, size_t max_size)
{
    // Récupération du path du SMO
    BOOLEAN delete_fstats = FALSE;
    if (!fstats)
    {
        fstats = malloc(sizeof(struct stat));
        delete_fstats = TRUE;
    }
    fstat(fd, fstats);
    char *env_prefix = getenv("RL_LOCK_PREFIX\n");
    if (env_prefix == NULL)
    {
        env_prefix = malloc(sizeof(char) * 2);
        env_prefix[0] = 'f';
        env_prefix[1] = '\0';
    }                                                      // Valeur par défaut
    debug("prefix from environment = '%s'\n", env_prefix); // DEBUG
    char *prefix = malloc(sizeof(char) * (1 + strlen(env_prefix) + 1));
    strcpy(prefix, "/");
    strcat(prefix, env_prefix);
    // Construction du path du SMO
    char *fd_dev = malloc(sizeof(char) * max_size);
    char *fd_ino = malloc(sizeof(char) * max_size);
    sprintf(fd_dev, "%ld", fstats->st_dev);
    sprintf(fd_ino, "%ld", fstats->st_ino);
    strcpy(smo_path, prefix);
    strcat(smo_path, "_");
    strcat(smo_path, fd_dev);
    strcat(smo_path, "_");
    strcat(smo_path, fd_ino);
    // Libération de la mémoire
    free(fd_dev);
    free(fd_ino);
    free(prefix);
    free(env_prefix);
    if (delete_fstats)
        free(fstats);
    return strlen(smo_path);
}

/**
 * La fonction qui vérifie si un thread est toujours en vie
 */
static BOOLEAN rl_is_thread_alive(pid_t pid)
{
    if (kill(pid, 0) == 0)
        return TRUE;
    else
        return FALSE;
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
rl_descriptor rl_open(const char *path, int flags, mode_t mode)
{
    // Ouverture "physique" du fichier
    debug("opening file on disk\n");
    int fd = open(path, flags, mode);
    rl_descriptor rl_fd;
    rl_fd.file_descriptor = -1;
    rl_fd.rl_file = NULL;
    if (fd < 0)
    { // Le fichier physique n'existe pas
        error("couldn't open file on disk\n");
        close(fd);
        return rl_fd;
    }
    debug("file (%d) opened successfully\n", fd); // DEBUG
    // Récupération des informations du fichier
    struct stat fstats;
    debug("getting file stats\n");
    fstat(fd, &fstats);
    debug("constructing shared memory object path\n");
    debug("file descriptor dev block = %ld, inode = %ld\n", fstats.st_dev, fstats.st_ino); // DEBUG
    char smo_path[DEV_INO_MAX_SIZE];
    int written = rl_path(smo_path, fd, &fstats, DEV_INO_MAX_SIZE);
    smo_path[written] = '\0';
    // Récupération du prefixe du SMO
    ok("final shared memory object path = '%s'\n", smo_path); // DEBUG
    // Ouverture du SMO
    debug("opening and/or creating shared memory object\n");
    int smo_fd = shm_open(smo_path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    BOOLEAN smo_was_on_disk = FALSE;
    if (smo_fd == -1 && errno == EEXIST)
    {
        // Le SMO existe déjà, on l'ouvre de nouveau sans O_CREAT
        smo_fd = shm_open(smo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if (smo_fd == -1)
        {
            error("couldn't open shared memory object\n");
            close(smo_fd);
            return rl_fd;
        }
        ok("shared memory object exists, opened with fd = %d\n", smo_fd); // DEBUG
        smo_was_on_disk = TRUE;
    }
    else if (smo_fd == -1)
    { // Le SMO n'existe pas et on n'a pas pu le créer
        error("couldn't create shared memory object\n");
        close(smo_fd);
        return rl_fd; // On retourne une structure vide avec un file_descriptor à -1 et un rl_file à NULL pour indiquer une erreur
    }
    if (!smo_was_on_disk)
    {                                                                                                     // Le SMO n'existait pas, on le crée et on le tronque à la taille de la structure rl_open_file
        info("shared memory object doesn't exist, creating & truncating to %ld\n", sizeof(rl_open_file)); // DEBUG
        ftruncate(smo_fd, sizeof(rl_open_file));
    }
    debug("mapping shared memory object in memory\n");
    // Map le fichier en mémoire - là, on a un pointeur sur la structure rl_open_file qui est mappée en mémoire à travers le SMO
    void *mmap_ptr = mmap(NULL, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, smo_fd, 0);
    if (mmap_ptr == (void *)MAP_FAILED)
    {
        error("couldn't map shared memory object in memory\n");
        rl_fd.file_descriptor = -1;
        rl_fd.rl_file = NULL;
        return rl_fd;
    }
    rl_open_file *rl_mapped_file = (rl_open_file *)mmap_ptr;
    ok("address of mapped file = %p\n", (void *)rl_mapped_file); // DEBUG
    if (!smo_was_on_disk)
    {                                                                              // Si le SMO n'existe pas, remplir les valeurs par défaut de rl_open_file (énoncé)
        debug("shared memory object didn't exist, filling with default values\n"); // DEBUG
        if (rl_mapped_file)
            debug("mapped file is ok\n"); // DEBUG
        pthread_mutex_t mutex;
        rl_mutex_init(&mutex);
        rl_mapped_file->mutex = mutex;
        rl_mapped_file->smo_fd = smo_fd;
        rl_mapped_file->first_lock = -2; // (pas sûr)
        rl_mapped_file->open_instances = 1;
        debug("first lock index set to %d\n", rl_mapped_file->first_lock); // DEBUG
        for (size_t i = 0; i < NB_LOCKS; i++)
        {
            pthread_cond_t cond1;
            rl_cond_init(&cond1);
            // debug("handling lock n°%ld, \n", i);	// DEBUG\n");
            rl_lock lock;
            lock.readers = 0;
            lock.writers = 0;
            // Valeurs par défaut
            lock.next_lock = -2;
            /** TODO: A vérifier/modifier */
            {
                lock.starting_offset = -1;
                lock.length = -1;
                lock.type = -1;
                lock.owners_count = 0;
                lock.cond = cond1;
                // debug("starting_offset = %ld, length = %ld, type = %d, owners_count = %ld\n", lock.starting_offset, lock.length, lock.type, lock.owners_count);	// DEBUG
            }
            rl_mapped_file->lock_table[i] = lock;
        }
    }
    else
    {
        pthread_mutex_t mutex;
        rl_mutex_init(&mutex);
        rl_mapped_file->mutex = mutex;
        pthread_cond_t cond;
        rl_cond_init(&cond);
    }
    /** TODO: On met à jour la structure de retour - mais quel descripteur? celui du fichier ou celui du SMO? */
    rl_fd.file_descriptor = fd; // ou smo_fd?
    rl_fd.rl_file = rl_mapped_file;
    ok("shared memory object mapped in memory\n"); // DEBUG
    // On met à jour la structure qui contient tous les rl_open_file(s) - on vérifie avant si le fichier n'est pas déjà ouvert
    debug("checking if file is already opened?\n");
    for (size_t i = 0; i < rl_all_files.files_count; i++)
    {
        /** TODO: Est ce qu'on doit vérifier l'adresse de la structure ou juste le pointeur vers la structure? Ou même vérifier ça tout court? */
        debug("&rl_all_files.open_files[i] = %p vs %p = rl_mapped_file\n", rl_all_files.open_files[i], rl_mapped_file); // DEBUG
        if (&rl_all_files.open_files[i] == &rl_mapped_file)
        {
            rl_mapped_file->open_instances++;
            debug("open instances of %p = %d\n", &rl_mapped_file, rl_mapped_file->open_instances);
            return rl_fd;
        }
    } // Si on arrive ici, c'est que le fichier n'est pas déjà ouvert
    debug("no, appending to opened files\n");
    rl_all_files.open_files[rl_all_files.files_count] = rl_mapped_file;
    rl_all_files.files_count += 1;
    ok("open instances of %p = %d\n", &rl_mapped_file, rl_mapped_file->open_instances);
    return rl_fd;
}

int rl_close(rl_descriptor rl_fd)
{
    rl_descriptor descriptor = rl_fd;
    pthread_mutex_lock(&(descriptor.rl_file->mutex));
    char smo_path[DEV_INO_MAX_SIZE];
    int written = rl_path(smo_path, rl_fd.file_descriptor, NULL, DEV_INO_MAX_SIZE);
    smo_path[written] = '\0';
    int ret = close(rl_fd.file_descriptor);
    if (ret != 0)
    {
        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
        return ret;
    }
    // On récupère le lock owner courant
    if (descriptor.rl_file->first_lock == -2)
    {
        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
        return ret;
    }
    rl_lock current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
    BOOLEAN has_next = TRUE;
    int previous_index[2] = {-3, descriptor.rl_file->first_lock};
    while (has_next)
    {
        debug("current lock has %ld owner(s)\n", current_lock.owners_count);
        for (int j = 0; j < current_lock.owners_count; j++)
        {
            // On enleve le proprietaire du lock
            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
            {
                // On enleve le lock du thread courant
                current_lock.lock_owners[j].thread_id = -1,
                current_lock.lock_owners[j].file_descriptor = -1;
                for (int k = j; k < current_lock.owners_count - 1; k++)
                {
                    current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                }
                current_lock.owners_count--;
                if (current_lock.type == F_RDLCK)
                {
                    current_lock.readers--;
                }
                else
                {
                    current_lock.writers--;
                }
                int index = rl_lock_check(&current_lock);
                if (index != -3)
                {
                    debug("index is not -3\n");

                    if (previous_index[0] == -3)
                    { // Si on doit supprimer le premier verrou dans la table
                        if (index == -1)
                        {
                            descriptor.rl_file->first_lock = -2;
                            has_next = FALSE;
                            break;
                        }
                        debug("lock has index %d in table\n", index);
                        descriptor.rl_file->first_lock = index;
                        previous_index[1] = index;
                        current_lock = descriptor.rl_file->lock_table[index];
                        info("brodcasting condition alteration to %p\n", &descriptor.rl_file->lock_table[index].cond);
                        pthread_cond_broadcast(&descriptor.rl_file->lock_table[index].cond);
                        break;
                    }
                    // Si on est arrivé au dernier élément
                    if (index == -1)
                    {
                        has_next = FALSE;
                        break;
                    }
                    else
                    {
                        descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                        previous_index[0] = previous_index[1];
                        previous_index[1] = index;
                        current_lock = descriptor.rl_file->lock_table[index];
                        break;
                    }
                }
                // Si il y a un prochain verrou
                if (current_lock.next_lock >= 0)
                {
                    info("ici\n");
                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    previous_index[0] = previous_index[1];
                    previous_index[1] = current_lock.next_lock;
                    break;
                }
                else
                {
                    has_next = FALSE;
                    break;
                }
            }
        }
        if (current_lock.next_lock >= 0)
        {
            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
            previous_index[0] = previous_index[1];
            previous_index[1] = current_lock.next_lock;
            break;
        }
        else
        {
            has_next = FALSE;
            break;
        }
    }
    if (descriptor.rl_file->open_instances == 0)
    {
        if (shm_unlink(smo_path) != 0)
        {
            error("could not unlink shared memory object @ '%s'\n", smo_path);
        }
    }
    else
    {
        descriptor.rl_file->open_instances--;
        ok("%p has %d open instances left\n", &descriptor.rl_file, descriptor.rl_file->open_instances);
        if (descriptor.rl_file->open_instances == 0)
        {
            if (shm_unlink(smo_path) != 0)
            {
                error("could not unlink shared memory object @ '%s'\n", smo_path);
            }
        }
    }
    pthread_mutex_unlock(&(descriptor.rl_file->mutex));
    return ret;
}

int rl_fcntl(rl_descriptor descriptor, int command, struct flock *lock)
{
    size_t lock_length = lock->l_len;
    if (lock_length == 0)
    {
        struct stat statbuf;
        fstat(descriptor.file_descriptor, &statbuf);
        lock_length = statbuf.st_size - lock->l_start;
    }
    debug("lock length = %ld\n", lock_length);
    pthread_mutex_lock(&(descriptor.rl_file->mutex));
    rl_lock_owner lock_owner = {.thread_id = getpid(), .file_descriptor = descriptor.file_descriptor};
    switch (command)
    {
    case F_SETLK:
        if (descriptor.rl_file->first_lock == -2 && (lock->l_type == F_RDLCK || lock->l_type == F_WRLCK))
        {
            // Si il n'y a aucun verrou sur le fichier, on pose direct
            descriptor.rl_file->first_lock = 0;
            descriptor.rl_file->lock_table[0].starting_offset = lock->l_start;
            descriptor.rl_file->lock_table[0].length = lock->l_len;
            descriptor.rl_file->lock_table[0].type = lock->l_type;
            descriptor.rl_file->lock_table[0].next_lock = -1;
            descriptor.rl_file->lock_table[0].owners_count = 1;
            descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
            switch (lock->l_type)
            {
            case F_RDLCK:
                ok("read lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                descriptor.rl_file->lock_table[0].readers = 1;
                break;
            case F_WRLCK:
                ok("write lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                descriptor.rl_file->lock_table[0].writers = 1;
                break;
            }
            pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
            pthread_mutex_unlock(&(descriptor.rl_file->mutex));
            return 0;
        }
        // On recupère le type d'opération
        switch (lock->l_type)
        {
        case F_RDLCK:
            info("requesting a read lock\n");
            print_flock(lock); // DEBUG
            // F_RDLCK: On vérifie si le fichier est déjà locké en écriture
            rl_lock current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            long int region_start = lock->l_start;
            long int region_end = lock->l_start + lock->l_len;
            BOOLEAN has_next = TRUE;
            while (has_next)
            {
                debug("looking for the current lock's (%d) owner\n", current_lock.next_lock);
                size_t lengthCurrent = current_lock.length;
                if (lengthCurrent == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    lengthCurrent = statbuf.st_size - current_lock.starting_offset;
                }
                // BOUCLE VERIFIANT SI IL Y A DES VERROUS QUI EMPECHENT DE POSER LE NOTRE.
                if (current_lock.length != 0 && current_lock.starting_offset + lengthCurrent <= lock->l_start)
                {
                    // Si le verrou observé finit avant notre intervalle
                    if (current_lock.next_lock >= 0)
                    { // Si y'a un verrou après on passe au suivant
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                    { // Si y'a pas de verrou apres alors on peut poser tranquillement car la liste des verrous est triée dans l'ordre de start
                        for (int i = 0; i < NB_LOCKS; i++)
                        {
                            if (descriptor.rl_file->lock_table[i].next_lock == -2)
                            {
                                descriptor.rl_file->lock_table[i].readers += 1;
                                descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                descriptor.rl_file->lock_table[i].length = lock->l_len;
                                descriptor.rl_file->lock_table[i].type = lock->l_type;
                                descriptor.rl_file->lock_table[i].owners_count = 1;
                                descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[i].readers += 1;
                                // current_lock.next_lock = i;
                                for (int j = 0; j < NB_LOCKS; j++)
                                {
                                    if (descriptor.rl_file->lock_table[j].next_lock == -1)
                                    {
                                        descriptor.rl_file->lock_table[j].next_lock = i;
                                        break;
                                    }
                                }
                                descriptor.rl_file->lock_table[i].next_lock = -1;
                                ok("read lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                                pthread_cond_broadcast(&(current_lock.cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }
                        }
                        errno = ENOLCK;
                        error("no more locks available\n");
                        pthread_cond_broadcast(&(current_lock.cond));
                        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                        return -1;
                    }
                }
                else if (current_lock.starting_offset >= lock->l_start + lock_length)
                { // Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée.
                    break;
                }
                else
                { // Ici, le verrou observé a une intersection non vide avec notre intervalle

                    if (current_lock.type == F_WRLCK)
                    { // si c'est un verrou en ecriture
                        BOOLEAN same_owner = 0;
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // si le lock appartient au thread courant:
                                same_owner = TRUE;
                                // On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                if (current_lock.starting_offset <= region_start)
                                    region_start = current_lock.starting_offset;
                                // On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                if (current_lock.starting_offset + lengthCurrent >= region_end)
                                    region_end = current_lock.starting_offset + lengthCurrent;
                                if (current_lock.next_lock >= 0)
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                if (current_lock.next_lock == -1)
                                    has_next = FALSE;
                                break;
                            }
                        }
                        if (!same_owner)
                        { // Le verrou en ecriture ne nous appartient pas, on ne peut pas lire.
                            errno = EAGAIN;
                            error("lock already taken\n");
                            pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                            return -1;
                        }
                    }
                    else if (current_lock.type == F_RDLCK)
                    { // Si c'est un verrou en lecture
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // Si le lock appartient au thread courant, on met a jour les bornes pour la fusion
                                if (current_lock.starting_offset <= region_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + lengthCurrent >= region_end)
                                    region_end = current_lock.starting_offset + lengthCurrent;
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break;
                                }
                                else if (current_lock.next_lock == -1)
                                    has_next = FALSE;
                            }
                        }
                        if (current_lock.next_lock >= 0)
                        {
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            break;
                        }
                        else if (current_lock.next_lock == -1)
                            has_next = FALSE;
                    }
                }
            }
            // Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle
            // On regarde déjà si il y a deja un verrou en lecture sur l'intervalle qu'on veut
            // On enleve les verrous qui vont etre fusionnés
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            int previous_index[2] = {-3, descriptor.rl_file->first_lock}; // pour maintenir à jour la liste chainée
            while (has_next)
            {
                size_t current_lock_length = current_lock.length;
                if (current_lock_length == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    current_lock_length = statbuf.st_size - current_lock.starting_offset;
                }
                // BOUCLE QUI ENLEVE L'OWNING D'UN VERROU SUR L'INTERVALLE : on enleve l'accès à un verrou pour rentrer dans un verrou qui sera plus global.
                // si c'est un verrou en ecriture, on part du verrou et on rejoint le plus gros verrou en lecture (rejoindre = plus tard dans le code)
                if (current_lock.starting_offset + current_lock_length < lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                    {
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                        has_next = FALSE;
                }
                else if (current_lock.starting_offset >= lock->l_start + lock_length)
                    break;
                else
                {
                    for (int j = 0; j < current_lock.owners_count; j++)
                    {
                        // On enleve le proprietaire du lock
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // On enleve le lock du thread courant
                            current_lock.lock_owners[j].thread_id = -1,
                            current_lock.lock_owners[j].file_descriptor = -1;
                            for (int k = j; k < current_lock.owners_count - 1; k++)
                            {
                                current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                            }
                            current_lock.owners_count--;
                            if (current_lock.type == F_RDLCK)
                            {
                                current_lock.readers--;
                            }
                            else
                            {
                                current_lock.writers--;
                            }
                            int index = rl_lock_check(&current_lock); // Si le verrou devient vide, ça le "supprime" et retourne l'indice du verrou d'après. -3 si pas vide
                            if (index != -3)
                            {
                                info("lock %d deleted\n", previous_index[1]);
                                if (previous_index[0] == -3)
                                { // si on doit supprimer le premier verrou dans la table
                                    if (index == -1)
                                    { // Si il n'y avait que ce verrou
                                        info("premier et dernier verrrou supprimé\n");
                                        descriptor.rl_file->first_lock = -2;
                                        has_next = FALSE;
                                        debug("breaking\n"); // BAD LOL
                                        break;
                                    } // Le verrou qui devient le premier verrou
                                    descriptor.rl_file->first_lock = index;
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                                if (index == -1)
                                {
                                    has_next = FALSE;
                                    break;
                                }
                                else
                                {
                                    descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                            }
                            if (current_lock.next_lock >= 0)
                            {
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                previous_index[0] = previous_index[1];
                                previous_index[1] = current_lock.next_lock;
                                break;
                            }
                            else
                            {
                                has_next = FALSE;
                                break;
                            }
                        }
                    }
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                    }
                    else
                    {
                        has_next = FALSE;
                    }
                }
            }
            size_t final_length;
            struct stat statbuf;
            fstat(descriptor.file_descriptor, &statbuf);
            size_t file_length = statbuf.st_size;
            if (file_length == region_end) // Si on veut un verrou sur tout le fichier
            {
                final_length = 0;
            }
            else
            {
                final_length = region_end - region_start;
            }
            if (descriptor.rl_file->first_lock == -2)
            { // Si on a supprimé tous les verrous sur le fichier :
                descriptor.rl_file->first_lock = 0;
                descriptor.rl_file->lock_table[0].readers = 1; // HERE
                descriptor.rl_file->lock_table[0].writers = 0; // HERE
                descriptor.rl_file->lock_table[0].length = final_length;
                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                descriptor.rl_file->lock_table[0].next_lock = -1;
                descriptor.rl_file->lock_table[0].owners_count = 1;
                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                descriptor.rl_file->lock_table[0].type = F_RDLCK;
                ok("read lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                pthread_cond_broadcast(&descriptor.rl_file->lock_table[0].cond);
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0;
            }
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            { // On ajoute le lock au bon endroit dans la liste chainée
                if (current_lock.starting_offset < region_start)
                {
                    if (current_lock.next_lock == -1)
                        return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, final_length, F_RDLCK, getpid(), &descriptor, lock_owner);
                    else
                    {
                        if (descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset >= region_start)
                            return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, final_length, F_RDLCK, getpid(), &descriptor, lock_owner);
                        else
                        {
                            previous_index[0] = previous_index[1];
                            previous_index[1] = current_lock.next_lock;
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        }
                    }
                }
                else
                    return rl_add_lock(&current_lock, -2, descriptor.rl_file->first_lock, region_start, final_length, F_RDLCK, getpid(), &descriptor, lock_owner);
            }
            break;
        case F_WRLCK:
            info("requesting a write lock\n");
            print_flock(lock); // DEBUG
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            region_start = lock->l_start;
            region_end = lock->l_start + lock->l_len;
            has_next = TRUE;
            while (has_next)
            {
                size_t lengthCurrent = current_lock.length;
                if (lengthCurrent == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    lengthCurrent = statbuf.st_size - current_lock.starting_offset;
                }
                if (current_lock.starting_offset + lengthCurrent <= lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    else
                    {
                        for (int i = 0; i < NB_LOCKS; i++)
                        {
                            if (descriptor.rl_file->lock_table[i].next_lock == -2)
                            {
                                descriptor.rl_file->lock_table[i].readers += 1;
                                descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                descriptor.rl_file->lock_table[i].length = lock->l_len;
                                descriptor.rl_file->lock_table[i].type = lock->l_type;
                                descriptor.rl_file->lock_table[i].owners_count = 1;
                                descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[i].writers += 1;
                                // current_lock.next_lock = i;
                                for (int j = 0; j < NB_LOCKS; j++)
                                {
                                    if (descriptor.rl_file->lock_table[j].next_lock == -1)
                                    {
                                        descriptor.rl_file->lock_table[j].next_lock = i;
                                        break;
                                    }
                                }
                                descriptor.rl_file->lock_table[i].next_lock = -1;
                                ok("write lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                                pthread_cond_broadcast(&(current_lock.cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }
                        }
                        errno = ENOLCK;
                        error("no more locks available\n");
                        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                        return -1;
                    }
                }
                else if (current_lock.starting_offset >= lock->l_start + lock_length) // Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée.
                    break;
                else
                {
                    if (current_lock.type == F_RDLCK)
                    {
                        if (current_lock.owners_count > 1)
                        {
                            errno = EAGAIN;
                            error("read lock already present\n");
                            pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                            return -1;
                        }
                        else
                        { // Si il n'y a qu'un owner et que c'est le thread courant, on pourra poser un verrou par dessus.
                            if (current_lock.lock_owners[0].file_descriptor == descriptor.file_descriptor && current_lock.lock_owners[0].thread_id == getpid())
                            {
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + lengthCurrent >= region_end)
                                    region_end = current_lock.starting_offset + lengthCurrent;
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break;
                                }
                                else if (current_lock.next_lock == -1)
                                {
                                    has_next = FALSE;
                                }
                            }
                            else
                            {
                                errno = EAGAIN;
                                error("read lock already present\n");
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return -1;
                            }
                        }
                    }
                    else if (current_lock.type == F_WRLCK)
                    {
                        BOOLEAN same_owner = FALSE;
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // si le lock appartient au thread courant:
                                same_owner = TRUE;
                                // Mise a jour des bornes pour préparer la fusion
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + lengthCurrent >= region_end)
                                    region_end = current_lock.starting_offset + lengthCurrent;
                                if (current_lock.next_lock >= 0)
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                break;
                            }
                        }
                        if (!same_owner)
                        {
                            errno = EAGAIN;
                            error("write lock already present\n");
                            pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                            return -1;
                        }
                        if (current_lock.next_lock >= 0)
                        {
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            break;
                        }
                        if (current_lock.next_lock == -1)
                            has_next = FALSE;
                    }
                }
            }
            // Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle
            // On enleve les verrous qui vont etre fusionnés
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            {
                size_t lengthCurrent = current_lock.length;
                if (lengthCurrent == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    lengthCurrent = statbuf.st_size - current_lock.starting_offset;
                }
                if (current_lock.starting_offset + lengthCurrent < lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                    {
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                        has_next = FALSE;
                }
                else if (current_lock.starting_offset >= lock->l_start + lock_length)
                    break;
                else
                {
                    for (int j = 0; j < current_lock.owners_count; j++)
                    {
                        // On enleve le proprietaire du lock
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // On enleve le lock du thread courant
                            current_lock.lock_owners[j].thread_id = -1,
                            current_lock.lock_owners[j].file_descriptor = -1;
                            for (int k = j; k < current_lock.owners_count - 1; k++)
                                current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                            current_lock.owners_count--;
                            if (current_lock.type == F_RDLCK)
                            {
                                current_lock.readers--;
                            }
                            else
                            {
                                current_lock.writers--;
                            }
                            int index = rl_lock_check(&current_lock);
                            if (index != -3)
                            {
                                if (previous_index[0] == -3)
                                { // si on doit supprimer le premier verrou dans la table
                                    if (index == -1)
                                    {
                                        descriptor.rl_file->first_lock = -2;
                                        has_next = FALSE;
                                        break;
                                    }
                                    descriptor.rl_file->first_lock = index;
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                                if (index == -1)
                                {
                                    has_next = FALSE;
                                    break;
                                }
                                else
                                {
                                    descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                            }
                            if (current_lock.next_lock >= 0)
                            {
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                previous_index[0] = previous_index[1];
                                previous_index[1] = current_lock.next_lock;
                                break;
                            }
                            else
                            {
                                has_next = FALSE;
                                break;
                            }
                        }
                    }
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        break;
                    }
                    else
                    {
                        has_next = FALSE;
                        break;
                    }
                }
            }
            fstat(descriptor.file_descriptor, &statbuf);
            file_length = statbuf.st_size;
            if (file_length == region_end) // Si on veut un verrou sur tout le fichier
            {
                final_length = 0;
            }
            else
            {
                final_length = region_end - region_start;
            }

            if (descriptor.rl_file->first_lock == -2)
            { // Si on a supprimé tous les verrous sur le fichier :
                descriptor.rl_file->first_lock = 0;
                descriptor.rl_file->lock_table[0].readers = 0;
                descriptor.rl_file->lock_table[0].writers = 1;
                descriptor.rl_file->lock_table[0].length = final_length;
                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                descriptor.rl_file->lock_table[0].next_lock = -1;
                descriptor.rl_file->lock_table[0].owners_count = 1;
                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                descriptor.rl_file->lock_table[0].type = F_WRLCK;
                ok("write lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                pthread_cond_broadcast(&descriptor.rl_file->lock_table[0].cond);
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0;
            }
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            {
                if (current_lock.starting_offset < region_start)
                {
                    if (current_lock.next_lock == -1)
                        return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, final_length, F_WRLCK, getpid(), &descriptor, lock_owner);
                    else
                    {
                        if (descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset >= region_start)
                            return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, final_length, F_WRLCK, getpid(), &descriptor, lock_owner);
                        else
                        {
                            previous_index[0] = previous_index[1];
                            previous_index[1] = current_lock.next_lock;
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        }
                    }
                }
                else
                    return rl_add_lock(&current_lock, -2, descriptor.rl_file->first_lock, region_start, final_length, F_WRLCK, getpid(), &descriptor, lock_owner);
            }
            ok("done setting a r/w lock\n");
            break;
        case F_UNLCK:
            info("unlocking a lock\n");
            print_flock(lock); // DEBUG
            if (descriptor.rl_file->first_lock == -2)
            {
                error("no lock to unlock\n");
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0; //"réussit sans rien faire d'après sujet"
            }
            int unlockable = 0;
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            debug("next lock index = %d\n", current_lock.next_lock);
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next && unlockable == 0)
            {
                size_t lengthCurrent = current_lock.length;
                if (lengthCurrent == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    lengthCurrent = statbuf.st_size - current_lock.starting_offset;
                }
                if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + lengthCurrent >= lock->l_start + lock_length)
                { // Si on a un lock qui englobe l'intervalle a unlock
                    for (size_t j = 0; j < current_lock.owners_count; j++)
                    {
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // si le lock appartient au thread courant:
                            unlockable = 1;
                            break;
                        }
                    }
                }
                else
                {
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                    }
                    else
                    {
                        has_next = FALSE;
                    }
                }
            }
            if (unlockable == 0)
            {
                error("no lock to unlock\n");
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0; //"réussit sans rien faire d'après sujet"
            }
            else
            {
                debug("current lock [ start = %ld | previous[0] = %d | previous[1] = %d ]\n", current_lock.starting_offset, previous_index[0], previous_index[1]);
                size_t current_lock_length = current_lock.length;
                if (current_lock_length == 0)
                {
                    struct stat statbuf;
                    fstat(descriptor.file_descriptor, &statbuf);
                    current_lock_length = statbuf.st_size - current_lock.starting_offset;
                }
                short lock_type = current_lock.type;
                long int left_lock_start = current_lock.starting_offset;
                long int left_lock_length = lock->l_start - current_lock.starting_offset;
                long int right_lock_start = lock->l_start + lock_length;
                long int right_lock_length = current_lock.starting_offset + current_lock_length - (lock->l_start + lock_length);
                int extends_till_end = 0; // Pour savoir si le verrou a droite va jusqu'a la fin du fichier.
                fstat(descriptor.file_descriptor, &statbuf);
                file_length = statbuf.st_size;
                if (current_lock_length == file_length)
                {
                    extends_till_end = 1;
                }
                // On  unlock le lock englobant (on ajoutera les nouveaux locks après)
                for (int j = 0; j < current_lock.owners_count; j++)
                {
                    // On enleve le proprietaire du lock
                    if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                    {
                        // On enleve le lock du thread courant
                        current_lock.lock_owners[j].thread_id = -1,
                        current_lock.lock_owners[j].file_descriptor = -1;
                        if (current_lock.type == F_RDLCK)
                        {
                            current_lock.readers--;
                        }
                        else
                        {
                            current_lock.writers--;
                        }

                        for (int k = j; k < current_lock.owners_count - 1; k++)
                            current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                        current_lock.owners_count--;
                        int index = rl_lock_check(&current_lock);
                        debug("lock index = %d\n", index);
                        if (index != -3)
                        {
                            if (previous_index[0] == -3)
                            { // si on doit supprimer le premier verrou dans la table
                                if (index == -1)
                                {
                                    descriptor.rl_file->first_lock = -2;

                                    break;
                                }
                                descriptor.rl_file->first_lock = index;
                                previous_index[1] = index;
                                current_lock = descriptor.rl_file->lock_table[index];
                                break;
                            }
                            if (index == -1)
                            {
                                descriptor.rl_file->lock_table[previous_index[0]].next_lock = -1;

                                break;
                            }
                            else
                            {
                                info("previous index = %d\n", previous_index[0]);
                                descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                break;
                            }
                        }
                    }
                }
                // On verifie si on doit couper le lock en 2 (ou juste le rétrecir)

                if (left_lock_start < lock->l_start)
                { // Si il y a un verrou a ajouter a gauche
                    // Il se peut que current_lock ait été supprimé donc on le redéfinit
                    if (descriptor.rl_file->first_lock == -2)
                    {
                        // Si il n'y a plus aucun verroun on ajoute le verrou
                        descriptor.rl_file->lock_table[0].starting_offset = left_lock_start;
                        descriptor.rl_file->lock_table[0].length = left_lock_length;
                        descriptor.rl_file->lock_table[0].owners_count = 1;
                        descriptor.rl_file->lock_table[0].lock_owners[0].thread_id = getpid();
                        descriptor.rl_file->lock_table[0].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                        descriptor.rl_file->lock_table[0].next_lock = -1;
                        descriptor.rl_file->lock_table[0].type = lock_type;
                        descriptor.rl_file->first_lock = 0;
                    }
                    else
                    {
                        int locks_added = 0;
                        current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        previous_index[0] = -3;
                        previous_index[1] = descriptor.rl_file->first_lock;
                        has_next = TRUE;

                        while (has_next && current_lock.starting_offset <= left_lock_start)
                        {
                            if (current_lock.starting_offset == left_lock_start && current_lock.length == left_lock_length && current_lock.type == lock_type)
                            {
                                info("lock already exists in this area\n");
                                // On s'ajoute à ce lock là
                                current_lock.lock_owners[current_lock.owners_count].thread_id = getpid();
                                current_lock.lock_owners[current_lock.owners_count].file_descriptor = descriptor.file_descriptor;
                                current_lock.owners_count++;
                                locks_added = 1;
                            }
                            else
                            {
                                if (current_lock.next_lock >= 0)
                                {
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = current_lock.next_lock;
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    debug("previous indices: (%d, %d)\n", previous_index[0], previous_index[1]);
                                }
                                else
                                {
                                    has_next = FALSE;
                                }
                            }
                        }
                        // Si on arrive ici c'est qu'il n'y a pas de lock à l'endroit souhaité. On en crée donc un.
                        if (locks_added == 0)
                        {
                            for (ssize_t i = 0; i < NB_LOCKS; i++)
                            {
                                if (descriptor.rl_file->lock_table[i].next_lock == -2)
                                {
                                    descriptor.rl_file->lock_table[i].starting_offset = left_lock_start;
                                    descriptor.rl_file->lock_table[i].length = left_lock_length;
                                    descriptor.rl_file->lock_table[i].owners_count = 1;
                                    descriptor.rl_file->lock_table[i].lock_owners[0].thread_id = getpid();
                                    descriptor.rl_file->lock_table[i].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                                    descriptor.rl_file->lock_table[i].type = lock_type;
                                    if (lock_type == F_RDLCK)
                                        descriptor.rl_file->lock_table[i].readers += 1;
                                    else
                                        descriptor.rl_file->lock_table[i].writers += 1;
                                    if (previous_index[0] >= 0 && descriptor.rl_file->lock_table[previous_index[0]].starting_offset <= left_lock_start)
                                    {

                                        // On le met au bon endroit dans la liste chainée
                                        // Si le dernier verrou observé est le dernier de la liste chainée
                                        ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                        descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                        current_lock.next_lock = oldnext;
                                        break;
                                    }
                                    else
                                    { // Si on s'est arreté avant parce que le start etait trop grand
                                        if (previous_index[0] == -3)
                                        { // Si on s'est arreté au premier lock de la liste
                                            descriptor.rl_file->first_lock = i;
                                            descriptor.rl_file->lock_table[i].next_lock = previous_index[1];
                                            break;
                                        }
                                        else
                                        {
                                            ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                            descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                            current_lock.next_lock = oldnext;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    info("lock added on the left [ start = %ld | length = %ld ]\n", left_lock_start, left_lock_length);
                }
                if (right_lock_length > 0)
                {

                    if (descriptor.rl_file->first_lock == -2)
                    {
                        // Si il n'y a plus aucun verroun on ajoute le verrou
                        descriptor.rl_file->lock_table[0].starting_offset = right_lock_start;
                        if (extends_till_end == 1)
                            descriptor.rl_file->lock_table[0].length = 0;
                        else
                            descriptor.rl_file->lock_table[0].length = right_lock_length;
                        descriptor.rl_file->lock_table[0].owners_count = 1;
                        descriptor.rl_file->lock_table[0].lock_owners[0].thread_id = getpid();
                        descriptor.rl_file->lock_table[0].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                        descriptor.rl_file->lock_table[0].next_lock = -1;
                        descriptor.rl_file->lock_table[0].type = lock_type;
                        descriptor.rl_file->first_lock = 0;
                        pthread_mutex_unlock(&descriptor.rl_file->mutex);
                        info("lock added on the right [ start = %ld | length = %ld ]\n", right_lock_start, right_lock_length);
                        return 0;
                    }
                    else
                    {

                        int locks_added = 0;
                        current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        previous_index[0] = -3;
                        previous_index[1] = descriptor.rl_file->first_lock;
                        has_next = TRUE;
                        while (has_next && current_lock.starting_offset <= right_lock_start)
                        {
                            size_t lengthCurrent = current_lock.length;
                            if (lengthCurrent == 0)
                            {
                                struct stat statbuf;
                                fstat(descriptor.file_descriptor, &statbuf);
                                lengthCurrent = statbuf.st_size - current_lock.starting_offset;
                            }
                            if (current_lock.starting_offset == right_lock_start && lengthCurrent == right_lock_length && current_lock.type == lock_type)
                            {
                                // On s'ajoute à ce lock là
                                current_lock.lock_owners[current_lock.owners_count].thread_id = getpid();
                                current_lock.lock_owners[current_lock.owners_count].file_descriptor = descriptor.file_descriptor;
                                current_lock.owners_count++;
                                locks_added = 1;
                            }
                            else
                            {
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = current_lock.next_lock;
                                    debug("previous indices: (%d, %d)\n", previous_index[0], previous_index[1]);
                                }
                                else
                                {
                                    has_next = FALSE;
                                }
                            }
                        }
                        // Si on arrive ici c'est qu'il n'y a pas de lock à l'endroit souhaité. On en crée donc un.
                        if (locks_added == 0)
                        {
                            for (ssize_t i = 0; i < NB_LOCKS; i++)
                            {
                                if (descriptor.rl_file->lock_table[i].next_lock == -2)
                                {
                                    descriptor.rl_file->lock_table[i].starting_offset = left_lock_start;
                                    if (extends_till_end == 1)
                                        descriptor.rl_file->lock_table[i].length = 0;
                                    else
                                        descriptor.rl_file->lock_table[i].length = right_lock_length;
                                    descriptor.rl_file->lock_table[i].owners_count = 1;
                                    descriptor.rl_file->lock_table[i].lock_owners[0].thread_id = getpid();
                                    descriptor.rl_file->lock_table[i].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                                    descriptor.rl_file->lock_table[i].type = lock_type;
                                    if (lock_type == F_RDLCK)
                                        descriptor.rl_file->lock_table[i].readers += 1;
                                    else
                                        descriptor.rl_file->lock_table[i].writers += 1;
                                    if (previous_index[0] >= 0 && descriptor.rl_file->lock_table[previous_index[0]].starting_offset <= left_lock_start)
                                    { // On le met au bon endroit dans la liste chainée
                                        // Si le dernier verrou observé est le dernier de la liste chainée
                                        ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                        descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                        current_lock.next_lock = oldnext;
                                        break;
                                    }
                                    else
                                    { // Si on s'est arreté avant parce que le start etait trop grand
                                        if (previous_index[0] == -3)
                                        { // Si on s'est arreté au premier lock de la liste
                                            descriptor.rl_file->first_lock = i;
                                            descriptor.rl_file->lock_table[i].next_lock = previous_index[1];
                                            break;
                                        }
                                        else
                                        {
                                            ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                            descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                            current_lock.next_lock = oldnext;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        info("lock added on the right [ start = %ld | length = %ld ]\n", right_lock_start, right_lock_length);
                    }
                }
                info("locks added (or not) after unlocking\n");
                pthread_mutex_unlock(&descriptor.rl_file->mutex);
                return 0;
            }
        }
        break;
    case F_SETLKW:
        info("premier lock : %d\n", descriptor.rl_file->first_lock);
        if (descriptor.rl_file->first_lock == -2 && (lock->l_type == F_RDLCK || lock->l_type == F_WRLCK))
        {
            // Si il n'y a aucun verrou sur le fichier, on pose direct
            descriptor.rl_file->first_lock = 0;
            descriptor.rl_file->lock_table[0].starting_offset = lock->l_start;
            descriptor.rl_file->lock_table[0].length = lock->l_len;
            descriptor.rl_file->lock_table[0].type = lock->l_type;
            descriptor.rl_file->lock_table[0].next_lock = -1;
            descriptor.rl_file->lock_table[0].owners_count = 1;
            descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
            switch (lock->l_type)
            {
            case F_RDLCK:
                ok("read lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                descriptor.rl_file->lock_table[0].readers = 1;
                break;
            case F_WRLCK:
                ok("write lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                descriptor.rl_file->lock_table[0].writers = 1;
            }

            pthread_mutex_unlock(&(descriptor.rl_file->mutex));
            return 0;
        }
        // On recupère le type d'opération
        switch (lock->l_type)
        {
        case F_RDLCK:
            info("requesting a read lock\n");
            print_flock(lock); // DEBUG
            // F_RDLCK: On vérifie si le fichier est déjà locké en écriture
            rl_lock current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            long int region_start = lock->l_start;
            long int region_end = lock->l_len + lock->l_start;
            BOOLEAN has_next = TRUE;
            while (has_next)
            {
                // BOUCLE VERIFIANT SI IL Y A DES VERROUS QUI EMPECHENT DE POSER LE NOTRE.
                if (current_lock.starting_offset + current_lock.length < lock->l_start)
                { // Si le verrou observé finit avant notre intervalle
                    if (current_lock.next_lock >= 0)
                    { // Si y'a un verrou après on passe au suivant
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                    { // Si y'a pas de verrou apres alors on peut poser tranquillement car la liste des verrous est triée dans l'ordre de start
                        for (int i = 0; i < NB_LOCKS; i++)
                        {
                            if (descriptor.rl_file->lock_table[i].next_lock == -2)
                            {
                                descriptor.rl_file->lock_table[i].readers += 1;
                                descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                descriptor.rl_file->lock_table[i].length = lock->l_len;
                                descriptor.rl_file->lock_table[i].type = lock->l_type;
                                descriptor.rl_file->lock_table[i].owners_count = 1;
                                descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[i].readers += 1;
                                // current_lock.next_lock = i;
                                for (int j = 0; j < NB_LOCKS; j++)
                                {
                                    if (descriptor.rl_file->lock_table[j].next_lock == -1)
                                    {
                                        descriptor.rl_file->lock_table[j].next_lock = i;
                                        break;
                                    }
                                }
                                descriptor.rl_file->lock_table[i].next_lock = -1;
                                ok("read lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                                pthread_cond_broadcast(&(current_lock.cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));

                                return 0;
                            }
                        }
                        /** TODO: à changer pour attendre la disponbilité d'un lock */
                        errno = ENOLCK;
                        error("no more locks available\n");
                        pthread_cond_broadcast(&(current_lock.cond));
                        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                        return -1;
                    }
                }
                else if (current_lock.starting_offset >= lock->l_start + lock->l_len)
                { // Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée.
                    break;
                }
                else
                { // Ici, le verrou observé a une intersection non vide avec notre intervalle
                    if (current_lock.type == F_WRLCK)
                    { // si c'est un verrou en ecriture
                        BOOLEAN same_owner = 0;
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // si le lock appartient au thread courant:
                                same_owner = TRUE;
                                // On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                // On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                if (current_lock.starting_offset + current_lock.length >= region_end)
                                    region_end = current_lock.starting_offset + current_lock.length;
                                if (current_lock.next_lock >= 0)
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                if (current_lock.next_lock == -1)
                                    has_next = FALSE;
                                break;
                            }
                        }
                        if (!same_owner)
                        { // Le verrou en ecriture ne nous appartient pas, on ne peut pas lire.
                            while (current_lock.writers > 0)
                            {
                                info("waiting for a write lock to be released\n");
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                pthread_cond_wait(&current_lock.cond, &(descriptor.rl_file->mutex));
                                debug("mutex unlocked\n");
                            }
                            if (descriptor.rl_file->first_lock == -2)
                            {
                                descriptor.rl_file->first_lock = 0;
                                descriptor.rl_file->lock_table[0].length = region_end - region_start;
                                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[0].next_lock = -1;
                                descriptor.rl_file->lock_table[0].owners_count = 1;
                                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                                descriptor.rl_file->lock_table[0].type = F_RDLCK;
                                ok("rd lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                                descriptor.rl_file->lock_table[0].readers = 1;
                                descriptor.rl_file->lock_table[0].writers = 0;
                                pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }

                            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        }
                    }
                    else if (current_lock.type == F_RDLCK)
                    { // Si c'est un verrou en lecture
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // Si le lock appartient au thread courant, on met a jour les bornes pour la fusion
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + current_lock.length >= region_end)
                                    region_end = current_lock.starting_offset + current_lock.length;
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break;
                                }
                                else if (current_lock.next_lock == -1)
                                    has_next = FALSE;
                            }
                        }
                        if (current_lock.next_lock >= 0)
                        {
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            break;
                        }
                        else if (current_lock.next_lock == -1)
                            has_next = FALSE;
                    }
                }
            }
            // Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle
            // On regarde déjà si il y a deja un verrou en lecture sur l'intervalle qu'on veut
            // On enleve les verrous qui vont etre fusionnés
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            int previous_index[2] = {-3, descriptor.rl_file->first_lock}; // pour maintenir à jour la liste chainée
            while (has_next)
            {
                // BOUCLE QUI ENLEVE L'OWNING D'UN VERROU SUR L'INTERVALLE : on enleve l'accès à un verrou pour rentrer dans un verrou qui sera plus global.
                // si c'est un verrou en ecriture, on part du verrou et on rejoint le plus gros verrou en lecture (rejoindre = plus tard dans le code)
                if (current_lock.starting_offset + current_lock.length < lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                    {
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                        has_next = FALSE;
                }
                else if (current_lock.starting_offset >= lock->l_start + lock->l_len)
                    break;
                else
                {
                    for (int j = 0; j < current_lock.owners_count; j++)
                    {
                        // On enleve le proprietaire du lock
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // On enleve le lock du thread courant
                            current_lock.lock_owners[j].thread_id = -1,
                            current_lock.lock_owners[j].file_descriptor = -1;
                            for (int k = j; k < current_lock.owners_count - 1; k++)
                            {
                                current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                            }
                            current_lock.owners_count--;
                            if (current_lock.type == F_RDLCK)
                            {
                                current_lock.readers--;
                            }
                            else
                            {
                                current_lock.writers--;
                            }
                            int index = rl_lock_check(&current_lock); // Si le verrou devient vide, ça le "supprime" et retourne l'indice du verrou d'après. -3 si pas vide
                            if (index != -3)
                            {
                                info("lock %d deleted\n", index);
                                if (previous_index[0] == -3)
                                { // si on doit supprimer le premier verrou dans la table
                                    if (index == -1)
                                    {
                                        descriptor.rl_file->first_lock = -2;
                                        has_next = FALSE;
                                        debug("breaking\n"); // BAD LOL
                                        break;
                                    } // Le verrou qui devient le premier verrou
                                    descriptor.rl_file->first_lock = index;
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                                if (index == -1)
                                {
                                    has_next = FALSE;
                                    break;
                                }
                                else
                                {
                                    descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                            }
                            if (current_lock.next_lock >= 0)
                            {
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                previous_index[0] = previous_index[1];
                                previous_index[1] = current_lock.next_lock;
                                break;
                            }
                            else
                            {
                                has_next = FALSE;
                                break;
                            }
                        }
                    }
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                    }
                    else
                    {
                        has_next = FALSE;
                    }
                }
            }
            if (descriptor.rl_file->first_lock == -2)
            { // Si on a supprimé tous les verrous sur le fichier :
                descriptor.rl_file->first_lock = 0;
                descriptor.rl_file->lock_table[0].length = region_end - region_start;
                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                descriptor.rl_file->lock_table[0].next_lock = -1;
                descriptor.rl_file->lock_table[0].owners_count = 1;
                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                descriptor.rl_file->lock_table[0].type = F_RDLCK;
                ok("read lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                descriptor.rl_file->lock_table[0].readers = 1;
                descriptor.rl_file->lock_table[0].writers = 0;
                pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0;
            }
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            { // On ajoute le lock au bon endroit dans la liste chainée
                if (current_lock.starting_offset < region_start)
                {
                    if (current_lock.next_lock == -1)
                        return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, region_end - region_start, F_RDLCK, getpid(), &descriptor, lock_owner);
                    else
                    {
                        if (descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset >= region_start)
                            return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, region_end - region_start, F_RDLCK, getpid(), &descriptor, lock_owner);
                        else
                        {
                            previous_index[0] = previous_index[1];
                            previous_index[1] = current_lock.next_lock;
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        }
                    }
                }
                else
                    return rl_add_lock(&current_lock, -2, descriptor.rl_file->first_lock, region_start, region_end - region_start, F_RDLCK, getpid(), &descriptor, lock_owner);
            }
            break;
        case F_WRLCK:
            info("requesting a write lock\n");
            print_flock(lock); // DEBUG
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            region_start = lock->l_start;
            region_end = lock->l_len + lock->l_start;
            has_next = TRUE;
            while (has_next)
            {

                if (current_lock.starting_offset + current_lock.length < lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    else
                    {
                        for (int i = 0; i < NB_LOCKS; i++)
                        {
                            info("next lock : %d\n", current_lock.next_lock);
                            if (descriptor.rl_file->lock_table[i].next_lock == -2)
                            {
                                descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                descriptor.rl_file->lock_table[i].length = lock->l_len;
                                descriptor.rl_file->lock_table[i].type = lock->l_type;

                                descriptor.rl_file->lock_table[i].owners_count = 1;
                                descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[i].writers += 1;
                                for (int j = 0; j < NB_LOCKS; j++)
                                {
                                    if (descriptor.rl_file->lock_table[j].next_lock == -1)
                                    {
                                        descriptor.rl_file->lock_table[j].next_lock = i;
                                        break;
                                    }
                                }
                                descriptor.rl_file->lock_table[i].next_lock = -1;
                                ok("write lock granted [ start = %ld | length = %ld ]\n", lock->l_start, lock->l_len);
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }
                        }
                        errno = ENOLCK;
                        error("no more locks available\n");
                        pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                        return -1;
                    }
                }
                else if (current_lock.starting_offset >= lock->l_start + lock->l_len) // Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée.
                    break;
                else
                {
                    if (current_lock.type == F_RDLCK)
                    {

                        if (current_lock.owners_count > 1)
                        {
                            while (current_lock.readers > 0)
                            {

                                debug("waiting for readers to release the lock\n");
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                pthread_cond_wait(&current_lock.cond, &(descriptor.rl_file->mutex));
                                debug("readers released the lock\n");
                            }

                            if (descriptor.rl_file->first_lock == -2)
                            {
                                descriptor.rl_file->first_lock = 0;
                                descriptor.rl_file->lock_table[0].length = region_end - region_start;
                                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[0].next_lock = -1;
                                descriptor.rl_file->lock_table[0].owners_count = 1;
                                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                                descriptor.rl_file->lock_table[0].type = F_WRLCK;
                                ok("wr lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                                descriptor.rl_file->lock_table[0].readers = 1;
                                descriptor.rl_file->lock_table[0].writers = 0;
                                pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }
                            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        }
                        else
                        { // Si il n'y a qu'un owner et que c'est le thread courant, on pourra poser un verrou par dessus.
                            if (current_lock.lock_owners[0].file_descriptor == descriptor.file_descriptor && current_lock.lock_owners[0].thread_id == getpid())
                            {
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + current_lock.length >= region_end)
                                    region_end = current_lock.starting_offset + current_lock.length;
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break;
                                }
                                else if (current_lock.next_lock == -1)
                                {
                                    has_next = FALSE;
                                }
                            }
                            else
                            {
                                info("lock readers : %d\n", current_lock.readers);
                                while (current_lock.readers > 0)
                                {
                                    info("waiting for readers to release the lock\n");
                                    pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                    pthread_cond_wait(&current_lock.cond, &(descriptor.rl_file->mutex));
                                    info("released");
                                }

                                if (descriptor.rl_file->first_lock == -2)
                                {
                                    descriptor.rl_file->first_lock = 0;
                                    descriptor.rl_file->lock_table[0].length = region_end - region_start;
                                    descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                                    descriptor.rl_file->lock_table[0].next_lock = -1;
                                    descriptor.rl_file->lock_table[0].owners_count = 1;
                                    descriptor.rl_file->lock_table[0].starting_offset = region_start;
                                    descriptor.rl_file->lock_table[0].type = F_WRLCK;
                                    ok("wr lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                                    descriptor.rl_file->lock_table[0].readers = 1;
                                    descriptor.rl_file->lock_table[0].writers = 0;
                                    pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
                                    pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                    return 0;
                                }
                                current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                            }
                        }
                    }
                    else if (current_lock.type == F_WRLCK)
                    {
                        info("write lock existing on the region\n");
                        BOOLEAN same_owner = FALSE;
                        for (size_t j = 0; j < current_lock.owners_count; j++)
                        {
                            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                            {
                                // si le lock appartient au thread courant:
                                info("owning the lock existing on the region\n");
                                same_owner = 1;
                                // Mise a jour des bornes pour préparer la fusion
                                if (current_lock.starting_offset <= lock->l_start)
                                    region_start = current_lock.starting_offset;
                                if (current_lock.starting_offset + current_lock.length >= region_end)
                                    region_end = current_lock.starting_offset + current_lock.length;
                                if (current_lock.next_lock >= 0)
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                break;
                            }
                        }
                        if (!same_owner)
                        {
                            info("current lock writer : %d\n", current_lock.writers);
                            while (current_lock.writers > 0)
                            {

                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                pthread_cond_wait(&current_lock.cond, &(descriptor.rl_file->mutex));
                                info("released");
                            }
                            if (descriptor.rl_file->first_lock == -2)
                            {
                                descriptor.rl_file->first_lock = 0;
                                descriptor.rl_file->lock_table[0].length = region_end - region_start;
                                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                                descriptor.rl_file->lock_table[0].next_lock = -1;
                                descriptor.rl_file->lock_table[0].owners_count = 1;
                                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                                descriptor.rl_file->lock_table[0].type = F_WRLCK;
                                ok("wr lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                                descriptor.rl_file->lock_table[0].readers = 1;
                                descriptor.rl_file->lock_table[0].writers = 0;
                                pthread_cond_broadcast(&(descriptor.rl_file->lock_table[0].cond));
                                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                                return 0;
                            }
                            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        }
                        if (current_lock.next_lock >= 0)
                        {
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            break;
                        }
                        if (current_lock.next_lock == -1)
                            has_next = FALSE;
                    }
                }
            }
            // Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle
            // On enleve les verrous qui vont etre fusionnés
            info("endwhile\n");
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            {
                if (current_lock.starting_offset + current_lock.length < lock->l_start)
                {
                    if (current_lock.next_lock >= 0)
                    {
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    }
                    else
                        has_next = FALSE;
                }
                else if (current_lock.starting_offset >= lock->l_start + lock->l_len)
                    break;
                else
                {
                    for (int j = 0; j < current_lock.owners_count; j++)
                    {

                        // On enleve le proprietaire du lock
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // On enleve le lock du thread courant
                            info("on enleve le lock du thread courant\n");
                            current_lock.lock_owners[j].thread_id = -1,
                            current_lock.lock_owners[j].file_descriptor = -1;
                            for (int k = j; k < current_lock.owners_count - 1; k++)
                                current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                            current_lock.owners_count--;
                            if (current_lock.type == F_RDLCK)
                            {
                                current_lock.readers--;
                            }
                            else
                            {
                                current_lock.writers--;
                            }
                            int index = rl_lock_check(&current_lock);

                            if (index != -3)
                            {

                                if (previous_index[0] == -3)
                                { // si on doit supprimer le premier verrou dans la table

                                    if (index == -1)
                                    {
                                        descriptor.rl_file->first_lock = -2;
                                        has_next = FALSE;
                                        break;
                                    }
                                    descriptor.rl_file->first_lock = index;
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                                if (index == -1)
                                {
                                    has_next = FALSE;
                                    break;
                                }
                                else
                                {
                                    descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = index;
                                    current_lock = descriptor.rl_file->lock_table[index];
                                    break;
                                }
                            }
                            if (current_lock.next_lock >= 0)
                            {
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                previous_index[0] = previous_index[1];
                                previous_index[1] = current_lock.next_lock;
                                break;
                            }
                            else
                            {
                                has_next = FALSE;
                                break;
                            }
                        }
                    }
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                        break;
                    }
                    else
                    {
                        has_next = FALSE;
                        break;
                    }
                }
            }
            if (descriptor.rl_file->first_lock == -2)
            { // Si on a supprimé tous les verrous sur le fichier :
                descriptor.rl_file->first_lock = 0;
                descriptor.rl_file->lock_table[0].length = region_end - region_start;
                descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
                descriptor.rl_file->lock_table[0].next_lock = -1;
                descriptor.rl_file->lock_table[0].owners_count = 1;
                descriptor.rl_file->lock_table[0].starting_offset = region_start;
                descriptor.rl_file->lock_table[0].type = F_WRLCK;
                ok("write lock granted [ start = %ld | length = %ld ]\n", region_start, region_end - region_start);
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0;
            }
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next)
            {
                if (current_lock.starting_offset < region_start)
                {
                    if (current_lock.next_lock == -1)
                        return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, region_end - region_start, F_WRLCK, getpid(), &descriptor, lock_owner);
                    else
                    {
                        if (descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset >= region_start)
                            return rl_add_lock(&current_lock, previous_index[1], current_lock.next_lock, region_start, region_end - region_start, F_WRLCK, getpid(), &descriptor, lock_owner);
                        else
                        {
                            previous_index[0] = previous_index[1];
                            previous_index[1] = current_lock.next_lock;
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        }
                    }
                }
                else
                    return rl_add_lock(&current_lock, -2, descriptor.rl_file->first_lock, region_start, region_end - region_start, F_WRLCK, getpid(), &descriptor, lock_owner);
            }
            ok("done setting a r/w lock\n");
            break;
        case F_UNLCK:
            info("unlocking a lock\n");
            print_flock(lock); // DEBUG
            if (descriptor.rl_file->first_lock == -2)
            {
                error("no lock to unlock\n");
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0; //"réussit sans rien faire d'après sujet"
            }
            int unlockable = 0;
            current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
            debug("next lock index = %d\n", current_lock.next_lock);
            has_next = TRUE;
            previous_index[0] = -3;
            previous_index[1] = descriptor.rl_file->first_lock;
            while (has_next && unlockable == 0)
            {

                if (current_lock.starting_offset <= lock->l_start && current_lock.starting_offset + current_lock.length >= lock->l_start + lock->l_len)
                { // Si on a un lock qui englobe l'intervalle a unlock
                    for (size_t j = 0; j < current_lock.owners_count; j++)
                    {
                        if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                        {
                            // si le lock appartient au thread courant:
                            unlockable = 1;
                            break;
                        }
                    }
                }
                else
                {
                    if (current_lock.next_lock >= 0)
                    {
                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                        previous_index[0] = previous_index[1];
                        previous_index[1] = current_lock.next_lock;
                    }
                    else
                    {
                        has_next = FALSE;
                    }
                }
            }
            if (unlockable == 0)
            {
                error("no lock to unlock\n");
                pthread_mutex_unlock(&(descriptor.rl_file->mutex));
                return 0; //"réussit sans rien faire d'après sujet"
            }
            else
            {
                short lock_type = current_lock.type;
                long int left_lock_start = current_lock.starting_offset;
                long int left_lock_length = lock->l_start - current_lock.starting_offset;
                long int right_lock_start = lock->l_start + lock->l_len;
                long int right_lock_length = current_lock.starting_offset + current_lock.length - (lock->l_start + lock->l_len);
                // On un unlock le lock englobant (on ajoutera les nouveaux locks après)
                for (int j = 0; j < current_lock.owners_count; j++)
                {
                    // On enleve le proprietaire du lock
                    if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor)
                    {
                        // On enleve le lock du thread courant
                        current_lock.lock_owners[j].thread_id = -1,
                        current_lock.lock_owners[j].file_descriptor = -1;
                        for (int k = j; k < current_lock.owners_count - 1; k++)
                            current_lock.lock_owners[k] = current_lock.lock_owners[k + 1];
                        current_lock.owners_count--;
                        if (current_lock.type == F_RDLCK)
                        {
                            current_lock.readers--;
                        }
                        else
                        {
                            current_lock.writers--;
                        }
                        if (current_lock.type == F_RDLCK)
                        {
                            current_lock.readers--;
                        }
                        else
                        {
                            current_lock.writers--;
                        }
                        int index = rl_lock_check(&current_lock);

                        if (index != -3)
                        {
                            if (previous_index[0] == -3)
                            { // si on doit supprimer le premier verrou dans la table
                                if (index == -1)
                                {
                                    descriptor.rl_file->first_lock = -2;
                                    has_next = FALSE;
                                    break;
                                }
                                descriptor.rl_file->first_lock = index;
                                previous_index[1] = index;
                                current_lock = descriptor.rl_file->lock_table[index];
                                break;
                            }
                            if (index == -1)
                            {
                                has_next = FALSE;
                                break;
                            }
                            else
                            {
                                descriptor.rl_file->lock_table[previous_index[0]].next_lock = index;
                                previous_index[0] = previous_index[1];
                                previous_index[1] = index;
                                current_lock = descriptor.rl_file->lock_table[index];
                                break;
                            }
                        }
                        if (current_lock.next_lock >= 0)
                        {
                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            previous_index[0] = previous_index[1];
                            previous_index[1] = current_lock.next_lock;
                            break;
                        }
                        else
                        {
                            has_next = FALSE;
                            break;
                        }
                    }
                }
                // On verifie si on doit couper le lock en 2 (ou juste le rétrecir)
                if (left_lock_start < lock->l_start)
                { // Si il y a un verrou a ajouter a gauche
                    // Il se peut que current_lock ait été supprimé donc on le redéfinit
                    if (descriptor.rl_file->first_lock == -2)
                    {
                        // Si il n'y a plus aucun verroun on ajoute le verrou
                        descriptor.rl_file->lock_table[0].starting_offset = left_lock_start;
                        descriptor.rl_file->lock_table[0].length = left_lock_length;
                        descriptor.rl_file->lock_table[0].owners_count = 1;
                        descriptor.rl_file->lock_table[0].lock_owners[0].thread_id = getpid();
                        descriptor.rl_file->lock_table[0].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                        descriptor.rl_file->lock_table[0].next_lock = -1;
                        descriptor.rl_file->lock_table[0].type = lock_type;
                        descriptor.rl_file->first_lock = 0;
                    }
                    else
                    {
                        int locks_added = 0;
                        current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        previous_index[0] = -3;
                        previous_index[1] = descriptor.rl_file->first_lock;
                        has_next = TRUE;
                        while (has_next && current_lock.starting_offset <= left_lock_start)
                        {
                            if (current_lock.starting_offset == left_lock_start && current_lock.length == left_lock_length && current_lock.type == lock_type)
                            {
                                // On s'ajoute à ce lock là
                                current_lock.lock_owners[current_lock.owners_count].thread_id = getpid();
                                current_lock.lock_owners[current_lock.owners_count].file_descriptor = descriptor.file_descriptor;
                                current_lock.owners_count++;
                                locks_added = 1;
                            }
                            else
                            {
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = current_lock.next_lock;
                                    debug("previous indices: (%d, %d)\n", previous_index[0], previous_index[1]);
                                }
                                else
                                {
                                    has_next = FALSE;
                                }
                            }
                        }
                        // Si on arrive ici c'est qu'il n'y a pas de lock à l'endroit souhaité. On en crée donc un.
                        if (locks_added == 0)
                        {
                            for (ssize_t i = 0; i < NB_LOCKS; i++)
                            {
                                if (descriptor.rl_file->lock_table[i].next_lock == -2)
                                {
                                    descriptor.rl_file->lock_table[i].starting_offset = left_lock_start;
                                    descriptor.rl_file->lock_table[i].length = left_lock_length;
                                    descriptor.rl_file->lock_table[i].owners_count = 1;
                                    descriptor.rl_file->lock_table[i].lock_owners[0].thread_id = getpid();
                                    descriptor.rl_file->lock_table[i].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                                    current_lock.type = lock_type;
                                    if (lock_type == F_RDLCK)
                                        descriptor.rl_file->lock_table[i].readers += 1;
                                    else
                                        descriptor.rl_file->lock_table[i].writers += 1;
                                    if (previous_index[0] >= 0 && descriptor.rl_file->lock_table[previous_index[0]].starting_offset <= left_lock_start)
                                    { // On le met au bon endroit dans la liste chainée
                                        // Si le dernier verrou observé est le dernier de la liste chainée
                                        ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                        descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                        current_lock.next_lock = oldnext;
                                        break;
                                    }
                                    else
                                    { // Si on s'est arreté avant parce que le start etait trop grand
                                        if (previous_index[0] == -3)
                                        { // Si on s'est arreté au premier lock de la liste
                                            descriptor.rl_file->first_lock = i;
                                            descriptor.rl_file->lock_table[i].next_lock = previous_index[1];
                                            break;
                                        }
                                        else
                                        {
                                            ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                            descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                            current_lock.next_lock = oldnext;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        info("lock added on the left [ start = %ld | length = %ld ]\n", left_lock_start, left_lock_length);
                    }
                }
                if (right_lock_length > 0)
                {
                    if (descriptor.rl_file->first_lock == -2)
                    {
                        // Si il n'y a plus aucun verroun on ajoute le verrou
                        descriptor.rl_file->lock_table[0].starting_offset = right_lock_start;
                        descriptor.rl_file->lock_table[0].length = right_lock_length;
                        descriptor.rl_file->lock_table[0].owners_count = 1;
                        descriptor.rl_file->lock_table[0].lock_owners[0].thread_id = getpid();
                        descriptor.rl_file->lock_table[0].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                        descriptor.rl_file->lock_table[0].next_lock = -1;
                        descriptor.rl_file->lock_table[0].type = lock_type;
                        descriptor.rl_file->first_lock = 0;
                        pthread_mutex_unlock(&descriptor.rl_file->mutex);
                        info("lock added on the right [ start = %ld | length = %ld ]\n", right_lock_start, right_lock_length);
                        return 0;
                    }
                    else
                    {
                        int locks_added = 0;
                        current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                        previous_index[0] = -3;
                        previous_index[1] = descriptor.rl_file->first_lock;
                        has_next = TRUE;
                        while (has_next && current_lock.starting_offset <= right_lock_start)
                        {
                            if (current_lock.starting_offset == right_lock_start && current_lock.length == right_lock_length && current_lock.type == lock_type)
                            {
                                // On s'ajoute à ce lock là
                                current_lock.lock_owners[current_lock.owners_count].thread_id = getpid();
                                current_lock.lock_owners[current_lock.owners_count].file_descriptor = descriptor.file_descriptor;
                                current_lock.owners_count++;
                                locks_added = 1;
                            }
                            else
                            {
                                if (current_lock.next_lock >= 0)
                                {
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    previous_index[0] = previous_index[1];
                                    previous_index[1] = current_lock.next_lock;
                                    debug("previous indices: (%d, %d)\n", previous_index[0], previous_index[1]);
                                }
                                else
                                {
                                    has_next = FALSE;
                                }
                            }
                        }
                        // Si on arrive ici c'est qu'il n'y a pas de lock à l'endroit souhaité. On en crée donc un.
                        if (locks_added == 0)
                        {
                            for (ssize_t i = 0; i < NB_LOCKS; i++)
                            {
                                if (descriptor.rl_file->lock_table[i].next_lock == -2)
                                {
                                    descriptor.rl_file->lock_table[i].starting_offset = left_lock_start;
                                    descriptor.rl_file->lock_table[i].length = left_lock_length;
                                    descriptor.rl_file->lock_table[i].owners_count = 1;
                                    descriptor.rl_file->lock_table[i].lock_owners[0].thread_id = getpid();
                                    descriptor.rl_file->lock_table[i].lock_owners[0].file_descriptor = descriptor.file_descriptor;
                                    current_lock.type = lock_type;
                                    if (lock_type == F_RDLCK)
                                        descriptor.rl_file->lock_table[i].readers += 1;
                                    else
                                        descriptor.rl_file->lock_table[i].writers += 1;
                                    if (previous_index[0] >= 0 && descriptor.rl_file->lock_table[previous_index[0]].starting_offset <= left_lock_start)
                                    { // On le met au bon endroit dans la liste chainée
                                        // Si le dernier verrou observé est le dernier de la liste chainée
                                        ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                        descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                        current_lock.next_lock = oldnext;
                                        break;
                                    }
                                    else
                                    { // Si on s'est arreté avant parce que le start etait trop grand
                                        if (previous_index[0] == -3)
                                        { // Si on s'est arreté au premier lock de la liste
                                            descriptor.rl_file->first_lock = i;
                                            descriptor.rl_file->lock_table[i].next_lock = previous_index[1];
                                            break;
                                        }
                                        else
                                        {
                                            ssize_t oldnext = descriptor.rl_file->lock_table[previous_index[0]].next_lock;
                                            descriptor.rl_file->lock_table[previous_index[0]].next_lock = i;
                                            current_lock.next_lock = oldnext;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        info("lock added on the right [ start = %ld | length = %ld ]\n", right_lock_start, right_lock_length);
                    }
                }
                info("locks added (or not) after unlocking\n");
                pthread_mutex_unlock(&descriptor.rl_file->mutex);
                return 0;
            }
        }
        break;
    case F_GETLK: /** TODO: implémenter? (pas nécessaire selon le sujet ) */
        break;
    }
    return 0;
}

rl_descriptor rl_dup2(rl_descriptor descriptor, int new_file_descriptor)
{
    info("duplicating descriptor from %d\n", descriptor.file_descriptor);
    // On duplique le descripteur de fichier
    rl_descriptor new_descriptor;
    // Nouveau lock owner
    rl_lock_owner new_owner = {.thread_id = getpid(), .file_descriptor = new_file_descriptor};
    // On cherche le lock owner dans la liste des lock owners
    for (size_t i = 0; i < NB_LOCKS; i++)
    {
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        if (current_lock.next_lock != -2)
        {
            size_t owners_count = current_lock.owners_count;
            info("owners count: %ld\n", owners_count); // DEBUG
            for (size_t j = 0; j < current_lock.owners_count; j++)
            {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid())
                {
                    debug("found lock owner at index %ld\n", j); // DEBUG
                    // On ajoute le nouveau lock owner
                    current_lock.lock_owners[owners_count] = new_owner;
                    current_lock.owners_count += 1;
                    debug("added new lock owner\n"); // DEBUG
                    break;
                }
            }
        }
    }
    // On retourne le nouveau descripteur de fichier
    new_descriptor.file_descriptor = new_file_descriptor;
    new_descriptor.rl_file = descriptor.rl_file;
    ok("duplicated descriptor to %d\n", new_file_descriptor);
    return new_descriptor;
}

rl_descriptor rl_dup(rl_descriptor descriptor)
{
    info("duplicating descriptor from %d\n", descriptor.file_descriptor);
    // On duplique le descripteur de fichier
    rl_descriptor new_descriptor;
    int new_file_descriptor = dup(descriptor.file_descriptor);
    // Nouveau lock owner
    rl_lock_owner new_owner = {.thread_id = getpid(), .file_descriptor = new_file_descriptor};
    // On cherche le lock owner dans la liste des lock owners
    for (size_t i = 0; i < NB_LOCKS; i++)
    {
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        if (current_lock.next_lock != -2)
        {
            info("current lock .next_lock = %d\n", current_lock.next_lock); // DEBUG
            size_t owners_count = current_lock.owners_count;
            info("owners count: %ld\n", owners_count); // DEBUG
            for (size_t j = 0; j < current_lock.owners_count; j++)
            {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid())
                {
                    debug("found lock owner at index %ld\n", j); // DEBUG
                    // On ajoute le nouveau lock owner
                    current_lock.lock_owners[owners_count] = new_owner;
                    current_lock.owners_count += 1;
                    debug("added new lock owner\n"); // DEBUG
                    break;
                }
            }
        }
    }
    // On retourne le nouveau descripteur de fichier
    new_descriptor.file_descriptor = new_file_descriptor;
    new_descriptor.rl_file = descriptor.rl_file;
    ok("duplicated descriptor to %d\n", new_file_descriptor);
    return new_descriptor;
}

pid_t rl_fork()
{
    info("forking\n");

    pid_t pid = fork();
    if (pid == 0)
    {
        // On est dans le fils
        pid_t parent = getppid();
        // On parcourt la liste des fichiers ouverts
        for (size_t i = 0; i < rl_all_files.files_count; i++)
        {
            // Si on trouve le parent dans la liste des lock owners du fichier courant (rl_all_files.files[i]) alors on ajoute le fils à la liste des lock owners
            rl_open_file *current_file = rl_all_files.open_files[i];
            for (size_t j = 0; j < NB_LOCKS; j++)
            {
                const size_t owners_count = current_file->lock_table[j].owners_count;
                for (size_t k = 0; k < owners_count; k++)
                {
                    if (current_file->lock_table[j].lock_owners[k].thread_id == parent)
                    {
                        pthread_mutex_lock(&(current_file->mutex));
                        // On ajoute le nouveau lock owner
                        rl_lock_owner new_owner = {.thread_id = getpid(), .file_descriptor = current_file->lock_table[j].lock_owners[k].file_descriptor};
                        current_file->lock_table[j].lock_owners[owners_count] = new_owner;
                        current_file->lock_table[j].owners_count += 1;
                        ok("added child process to lock owners\n");
                        pthread_mutex_unlock(&(current_file->mutex));
                        break;
                    }
                }
            }
        }
        kill(getppid(), SIGUSR1);
    }
    else if (pid < 0)
    {
        error("fork() failed\n");
    }
    else
    {
        // On est dans le père
        // On attend que le fils ait fini d'ajouter les lock owners
        struct sigaction sa;
        sa.sa_handler = &notify;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);
        pause();
    }

    return pid;
}

ssize_t rl_write(rl_descriptor descriptor, const void *buffer, size_t count)
{
    if (descriptor.rl_file->first_lock == -2)
    {
        // Si le fichier n'a pas de lock, on écrit
        return write(descriptor.file_descriptor, buffer, count);
    }
    // On parcourt la liste des locks du fichier
    for (size_t i = 0; i < NB_LOCKS; i++)
    {
        rl_lock current_lock = descriptor.rl_file->lock_table[i];
        debug("longueur : %ld\n", current_lock.length); // DEBUG
        long int lengthCurrent = current_lock.length;
        if (lengthCurrent == 0)
        {
            struct stat statbuf;
            fstat(descriptor.file_descriptor, &statbuf);
            lengthCurrent = statbuf.st_size - current_lock.starting_offset;
        }
        // Si le lock est de type F_WRLCK et que le thread courant est dans la liste des lock owners, alors on peut écrire
        if (current_lock.type == F_WRLCK)
        {
            const size_t owners_count = current_lock.owners_count;
            for (size_t j = 0; j < owners_count; j++)
            {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid())
                {
                    // On vérifie que la position du curseur est bien dans l'intervalle
                    if (lseek(descriptor.file_descriptor, 0, SEEK_CUR) < current_lock.starting_offset || lseek(descriptor.file_descriptor, 0, SEEK_CUR) + count > current_lock.starting_offset + lengthCurrent)
                    {
                        debug("bornes : %ld %ld %ld\n", lseek(descriptor.file_descriptor, 0, SEEK_CUR), current_lock.starting_offset, current_lock.starting_offset + lengthCurrent);
                        break;
                    }
                    // On vérifie que la taille du buffer est bien dans l'intervalle

                    // On déplace le curseur à la position starting_offset

                    // On écrit
                    return write(descriptor.file_descriptor, buffer, count);
                }
            }
        }
    }
    // Sinon on ne peut pas écrire
    info("write not allowed\n");
    errno = EACCES;
    return -1;
}

ssize_t rl_read(rl_descriptor descriptor, void *buffer, size_t count)
{
    if (descriptor.rl_file->first_lock == -2)
    {
        // Si le fichier n'a pas de lock, on lit
        return read(descriptor.file_descriptor, buffer, count);
    }
    // On parcourt la liste des locks du fichier
    for (int i = 0; i < NB_LOCKS; i++)
    {

        rl_lock current_lock = descriptor.rl_file->lock_table[i];

        size_t lengthCurrent = current_lock.length;
        if (lengthCurrent == 0)
        {
            struct stat statbuf;
            fstat(descriptor.file_descriptor, &statbuf);
            lengthCurrent = statbuf.st_size - current_lock.starting_offset;
        }
        info("i : %d seeks : %ld %ld %ld\n", i, lseek(descriptor.file_descriptor, 0, SEEK_CUR), current_lock.starting_offset, current_lock.starting_offset + lengthCurrent);
        // Si le lock est de type F_RDLCK et que le thread courant est dans la liste des lock owners, alors on peut lire
        if (current_lock.type == F_RDLCK)
        {

            const size_t owners_count = current_lock.owners_count;
            for (size_t j = 0; j < owners_count; j++)
            {
                rl_lock_owner current_lock_owner = current_lock.lock_owners[j];
                if (current_lock_owner.thread_id == getpid())
                {
                    // On vérifie que la position du curseur est bien dans l'intervalle
                    if (lseek(descriptor.file_descriptor, 0, SEEK_CUR) < current_lock.starting_offset || lseek(descriptor.file_descriptor, 0, SEEK_CUR) + count > current_lock.starting_offset + lengthCurrent)
                    {
                        break;
                    }
                    // On lit

                    return read(descriptor.file_descriptor, buffer, count);
                }
            }
        }
    }
    // Sinon on ne peut pas lire
    info("read not allowed\n");
    errno = EACCES;
    return -1;
}

int rl_init_library()
{
    // On initialise la liste des fichiers ouverts
    rl_all_files.files_count = 0;
    return 0;
}

int rl_execl(const char *path, const char *arg, ...)
{
    const char *smo_path = "/rl_smo";
    int smo_fd = shm_open(smo_path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    BOOLEAN smo_was_on_disk = FALSE;
    if (smo_fd == -1 && errno == EEXIST)
    {
        // Le SMO existe déjà, on l'ouvre de nouveau sans O_CREAT
        smo_fd = shm_open(smo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if (smo_fd == -1)
        {
            error("couldn't open shared memory object\n");
            close(smo_fd);
            return -1;
        }
        ok("shared memory object exists, opened with fd = %d\n", smo_fd); // DEBUG
        smo_was_on_disk = TRUE;
    }
    else if (smo_fd == -1)
    { // Le SMO n'existe pas et on n'a pas pu le créer
        error("couldn't create shared memory object\n");
        close(smo_fd);
        return -1;
    }
    if (!smo_was_on_disk)
    {                                                                                                                                                      // Le SMO n'existait pas, on le crée et on le tronque à la taille de la structure rl_open_file
        ok("shared memory object doesn't exist, creating & truncating to %ld\n", sizeof(rl_all_files) + rl_all_files.files_count * sizeof(rl_descriptor)); // DEBUG
        ftruncate(smo_fd, sizeof(rl_all_files) + rl_all_files.files_count * sizeof(rl_descriptor));
    }
    debug("mapping shared memory object in memory\n");
    // Map le fichier en mémoire - là, on a un pointeur sur la structure rl_open_file qui est mappée en mémoire à travers le SMO
    void *mmap_ptr = mmap(NULL, sizeof(rl_all_files) + rl_all_files.files_count * sizeof(rl_descriptor), PROT_READ | PROT_WRITE, MAP_SHARED, smo_fd, 0);
    if (mmap_ptr == (void *)MAP_FAILED)
    {
        error("couldn't map shared memory object in memory\n");
        return -1;
    }
    memcpy(mmap_ptr, &rl_all_files, sizeof(rl_all_files));
    for (size_t i = 0; i < rl_all_files.files_count; i++)
    {
        memcpy(mmap_ptr + sizeof(rl_all_files) + i * sizeof(rl_descriptor), &rl_all_files.open_files[i], sizeof(rl_descriptor));
    }
    // On ferme le SMO
    close(smo_fd);
    // On crée un tableau de char * pour les arguments
    va_list args;
    va_start(args, arg);
    int argc = 1;
    while (va_arg(args, char *) != NULL)
    {
        argc++;
    }
    va_end(args);
    // On crée un tableau de char * pour les arguments
    char *argv[argc + 1];
    argv[0] = (char *)arg;
    va_start(args, arg);
    for (size_t i = 1; i < argc; i++)
    {
        argv[i] = va_arg(args, char *);
    }
    va_end(args);
    argv[argc] = NULL;
    // On exécute le programme
    return execv(path, argv);
}