/**
 * JEDDI SKANDER - 21957008
 * MAZELET FLORENT - XXXXXXXX (TODO: A COMPLETER)
*/
//TODO trier la liste des rl_lock par ordre croissant de starting_offset
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
    rl_descriptor rl_fd;
    rl_fd.file_descriptor=-1; 
    rl_fd.rl_file=NULL;
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
    pthread_mutex_t mutex; 
    initialiser_mutex(&mutex);
    rl_fd.mutex = mutex; 
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
    rl_descriptor descriptor= rl_fd; 
    int ret = close(rl_fd.file_descriptor);
    info("file descriptor closed\n");
    // On récupère le lock owner courant
    info("removing lock owner & possibly lock\n");
    if(descriptor.rl_file->first_lock==-2){
        return ret; 
    }
    rl_lock current_lock  = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
    int hasnext=1; 
    int ancienIndice[2] = {-1, descriptor.rl_file->first_lock}; 
    while(hasnext){
        for(int j=0; j<current_lock.owners_count; j++){//On enleve le proprietaire du lock
            if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                //On enleve le lock du thread courant
                current_lock.lock_owners[j].thread_id = -1, 
                current_lock.lock_owners[j].file_descriptor = -1; 
                for(int k = j; k<current_lock.owners_count-1; k++){
                    current_lock.lock_owners[k] = current_lock.lock_owners[k+1];
                }
                current_lock.owners_count--;
                int index = check_empty_lock(&current_lock);
                if(index!=-3){
                    if(ancienIndice[0]==-1){//si on doit supprimer le premier verrou dans la table
                    if(index==-1){
                        descriptor.rl_file->first_lock=-2; 
                        hasnext=0; 
                        break; 
                    }
                        descriptor.rl_file->first_lock = index; 
                        descriptor.rl_file->lock_table[index].next_lock=-2;
                        descriptor.rl_file->lock_table[index].type=-1;
                        descriptor.rl_file->lock_table[index].starting_offset=-1;
                        descriptor.rl_file->lock_table[index].length=-1;
                        descriptor.rl_file->lock_table[index].owners_count=0;
                                            

                        ancienIndice[1]=index;
                        
                        current_lock = descriptor.rl_file->lock_table[index];
                        break; 
                    }
                    if(index==-1){
                    
                        hasnext=0; 
                        break; 
                    }
                    else{
                    descriptor.rl_file->lock_table[ancienIndice[0]].next_lock=index; 
                    ancienIndice[0]=ancienIndice[1];
                    ancienIndice[1]=index; 
                    current_lock = descriptor.rl_file->lock_table[index];
                    break;
                    }
                }
                if (current_lock.next_lock!=-1){
                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                    ancienIndice[0]=ancienIndice[1];
                    ancienIndice[1]=current_lock.next_lock;
                    break; 
                }
                else{
                    hasnext = 0; 
                    break; 
                }
            }
        }    
    }
    
    return ret;
}

int rl_fcntl(rl_descriptor descriptor, int command, struct flock* lock) {
    pthread_mutex_lock(&(descriptor.mutex)); 
    rl_lock_owner lock_owner = { .thread_id = getpid(), .file_descriptor = descriptor.file_descriptor };
    switch (command) {
        case F_SETLK: 
        if(descriptor.rl_file->first_lock==-2){
            //Si il n'y a aucun verrou sur le fichier, on pose direct
            descriptor.rl_file->first_lock = 0;
            descriptor.rl_file->lock_table[0].starting_offset = lock->l_start;
            descriptor.rl_file->lock_table[0].length = lock->l_len;
            descriptor.rl_file->lock_table[0].type = lock->l_type;
            descriptor.rl_file->lock_table[0].next_lock = -1;
            descriptor.rl_file->lock_table[0].owners_count = 1;
            descriptor.rl_file->lock_table[0].lock_owners[0] = lock_owner;
            pthread_mutex_unlock(&(descriptor.mutex));
            return 0;

        }
            // On recupère le type d'opération
            switch (lock->l_type) {
                case F_RDLCK:
                    info("requesting a read lock\n");   
                    print_flock(lock);	// DEBUG                 
                    // F_RDLCK: on vérifie si le fichier est déjà locké en écriture
                    rl_lock current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                    int newStart =lock->l_start;
                    int newEnd = lock->l_len+lock->l_start;
                    int hasnext=  1;
                    while(hasnext){//très moche mais je sais pas si on peut faire autrement (il est 23;55 je suis fatigué)
                        //BOUCLE VERIFIANT SI IL Y A DES VERROUS QUI EMPECHENT DE POSER LE NOTRE. 


                        if (current_lock.starting_offset+current_lock.length<lock->l_start){//Si le verrou observé finit avant notre intervalle
                                if(current_lock.next_lock!=-1){//si y'a un verrou après on passe au suivant
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                }
                                else{//Si y'a pas de verrou apres alors on peut poser tranquillement car la liste des verrous est triée dans l'ordre de start
                                    for(int i = 0; i<NB_LOCKS; i++){
                                        if(descriptor.rl_file->lock_table[i].next_lock==-2){
                                            descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                            descriptor.rl_file->lock_table[i].length = lock->l_len;
                                            descriptor.rl_file->lock_table[i].type = lock->l_type;
                                            descriptor.rl_file->lock_table[i].next_lock = -1;
                                            descriptor.rl_file->lock_table[i].owners_count = 1;
                                            descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                            current_lock.next_lock = i;
                                            info("rdlock added\n");
                                            pthread_mutex_unlock(&(descriptor.mutex));
                                            return 0;
                                        }
                                    }
                                    info("no more space for locks\n");
                                    pthread_mutex_unlock(&(descriptor.mutex));
                                    return -1; 
                                }
                            }
                        else if (current_lock.starting_offset>=lock->l_start+lock->l_len){//Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée. 
                            break; 
                        }
                        else{//Ici, le verrou observé a une intersection non vide avec notre intervalle
                            if (current_lock.type == F_WRLCK){//si c'est un verrou en ecriture
                                int courant = 0; 
                                for(size_t j = 0; j<current_lock.owners_count; j++){
                                    if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                                        //si le lock appartient au thread courant: 
                                         courant = 1; 
                                        if (current_lock.starting_offset <=lock->l_start){//On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                            newStart=current_lock.starting_offset;
                                            
                                          
                                        }
                                        if (current_lock.starting_offset+current_lock.length>=newEnd){//On définit la nouvelle borne pour la fusion de ce verrou (qui sera changé en verrou en lecture)
                                            newEnd=current_lock.starting_offset+current_lock.length;
                                          
                                        }
                                        if(current_lock.next_lock!=-1){
                                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                        }
                                        if(current_lock.next_lock==-1){
                                            hasnext = 0; 
                                        }
                                        break; 
                                    }
                                }
                                if(courant == 0){//Le verrou en ecriture ne nous appartient pas, on ne peut pas lire. 
                                    errno=EAGAIN;
                                    info("lock already taken\n");
                                    pthread_mutex_unlock(&(descriptor.mutex));
                                    return -1; 
                                }
                            }
                            else if (current_lock.type == F_RDLCK){//Si c'est un verrou en lecture
                                for(size_t j = 0; j<current_lock.owners_count; j++){
                                    if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                                        //si le lock appartient au thread courant: 
                                        if (current_lock.starting_offset <=lock->l_start){//On met a jour les bornes pour la fusion 
                                            newStart=current_lock.starting_offset;       
                                        }
                                        if (current_lock.starting_offset+current_lock.length>=newEnd){
                                            newEnd=current_lock.starting_offset+current_lock.length;    
                                        }
                                        if(current_lock.next_lock!=-1){
                                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                            break; 
                                        }
                                        if (current_lock.next_lock==-1){
                                            hasnext = 0; 
                                        }
                                        
                                    }
                                }
                                if(current_lock.next_lock!=-1){
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break; 
                                }
                                if (current_lock.next_lock==-1){
                                    hasnext = 0; 
                                }
                            }
                        }
                        
                       
                        
                    }
                    //Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle 
                    //On regarde déjà si il y a deja un verrou en lecture sur l'intervalle qu'on veut 
           
                
                    //On enleve les verrous qui vont etre fusionnés 
                    current_lock=descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                    hasnext=1; 
                    int ancienIndice[2] = {-1, descriptor.rl_file->first_lock}; //pour maintenir à jour la liste chainée
                    while(hasnext){
                        //BOUCLE QUI ENLEVE L'OWNING D'UN VERROU SUR L'INTERVALLE : on enleve l'accès à un verrou pour rentrer dans un verrou qui sera plus global. 
                        //si c'est un verrou en ecriture, on se part du verrou et on rejoint le plus gros verrou en lecture (rejoindre = plus tard dans le code)
                        if(current_lock.starting_offset+current_lock.length<lock->l_start){
                            if(current_lock.next_lock!=-1){
                                ancienIndice[0]= ancienIndice[1]; 
                                ancienIndice[1]=current_lock.next_lock; 
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            }
                            else{
                                hasnext = 0; 
                            }
                        }
                        else if (current_lock.starting_offset>=lock->l_start+lock->l_len){
                            break;
                        }
                        else{
                            for(int j=0; j<current_lock.owners_count; j++){
                                //On enleve le proprietaire du lock
                                if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                                    //On enleve le lock du thread courant
                                    current_lock.lock_owners[j].thread_id = -1, 
                                    current_lock.lock_owners[j].file_descriptor = -1; 
                                    for(int k = j; k<current_lock.owners_count-1; k++){
                                        current_lock.lock_owners[k] = current_lock.lock_owners[k+1];
                                    }
                                    current_lock.owners_count--;
                                    int index = check_empty_lock(&current_lock);//Si le verrou devient vide, ça le "supprime" et retourne l'indice du verrou d'après. -3 si pas vide
                                    if(index!=-3){
                                        if(ancienIndice[0]==-1){//si on doit supprimer le premier verrou dans la table
                                        if(index==-1){
                                            descriptor.rl_file->first_lock=-2; 
                                            hasnext=0; 
                                            break; 
                                        }//Le verrou qui devient le premier verrou 
                                            descriptor.rl_file->first_lock = index; 
                                            ancienIndice[1]=index;
                                            current_lock = descriptor.rl_file->lock_table[index];
                                            break; 
                                        }
                                        if(index==-1){
                                        
                                            hasnext=0; 
                                            break; 
                                        }
                                        else{
                                        descriptor.rl_file->lock_table[ancienIndice[0]].next_lock=index; 
                                        ancienIndice[0]=ancienIndice[1];
                                        ancienIndice[1]=index; 
                                        current_lock = descriptor.rl_file->lock_table[index];
                                        break;
                                        }
                                    }
                                    if (current_lock.next_lock!=-1){
                                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                        ancienIndice[0]=ancienIndice[1];
                                        ancienIndice[1]=current_lock.next_lock;
                                        break; 
                                    }
                                    else{
                                        hasnext = 0; 
                                        break; 
                                    }
                                }
                            }    
                            if (current_lock.next_lock!=-1){
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                ancienIndice[0]=ancienIndice[1];
                                ancienIndice[1]=current_lock.next_lock;
                                break; 
                            }
                            else{
                                hasnext = 0; 
                                break; 
                            }
                        }
                    }
                    if (descriptor.rl_file->first_lock==-1){//Si on a supprimé tous les verrous sur le fichier : 
                        descriptor.rl_file->first_lock=0;
                        descriptor.rl_file->lock_table[0].length= newEnd-newStart; 
                        descriptor.rl_file->lock_table[0].lock_owners[0]=lock_owner;
                        descriptor.rl_file->lock_table[0].next_lock=-1; 
                        descriptor.rl_file->lock_table[0].owners_count=1; 
                        descriptor.rl_file->lock_table[0].starting_offset=newStart;
                        descriptor.rl_file->lock_table[0].type=F_RDLCK;
                        info("rdlock added"); 
                        pthread_mutex_unlock(&(descriptor.mutex));
                        return 0; 
                    }
                    current_lock=descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                    hasnext=1; 
                    ancienIndice[0]=-1; 
                    ancienIndice[1]=descriptor.rl_file->first_lock;
                    while (hasnext){//On ajoute le lock au bon endroit dans la liste chainée
                        if(current_lock.starting_offset<newStart){
                            if (current_lock.next_lock==-1){
                                return addLock(&current_lock, ancienIndice[1],current_lock.next_lock, newStart, newEnd-newStart, F_RDLCK, getpid(), &descriptor, lock_owner);
                               
                            }
                            else{
                                if(descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset>=newStart){
                                    return addLock(&current_lock, ancienIndice[1],current_lock.next_lock, newStart, newEnd-newStart, F_RDLCK, getpid(), &descriptor, lock_owner);
                                    
                                }
                                else{
                                    ancienIndice[0]=ancienIndice[1];
                                    ancienIndice[1]=current_lock.next_lock;
                                    current_lock=descriptor.rl_file->lock_table[current_lock.next_lock];
                                }
                            }
                        }
                        else{
                            return addLock(&current_lock,-2, descriptor.rl_file->first_lock, newStart, newEnd-newStart, F_RDLCK, getpid(), &descriptor, lock_owner);
                            
                        }
                    }
                    
                    
                    

                    //todo : mieux découper: si on a un verrou en ecriture qui intersecte l'intervalle il faut le découper pour que dans l'intervalle il y ait 1 partie du verrou en lecture, et en dehors l'ecriture
                    //todo : regarder lors de l'ajout si il n'y a pas deja un verrou sur l'intervalle exact [newStart, newEnd] pour pouvoir se poser dessus directement et pas en recréer un. 
                break; 
                case F_WRLCK: 
                    info("requesting a write lock\n");   
                    print_flock(lock);	// DEBUG                 
                    current_lock = descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                     newStart =lock->l_start;
                     newEnd = lock->l_len+lock->l_start;
                     hasnext=  1;
                    while(hasnext){//très moche mais je sais pas si on peut faire autrement (il est 23;55 je suis fatigué)
                        if (current_lock.starting_offset+current_lock.length<lock->l_start){
                                if(current_lock.next_lock!=-1){
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                }
                                else{
                                    for(int i = 0; i<NB_LOCKS; i++){
                                        if(descriptor.rl_file->lock_table[i].next_lock==-2){
                                            descriptor.rl_file->lock_table[i].starting_offset = lock->l_start;
                                            descriptor.rl_file->lock_table[i].length = lock->l_len;
                                            descriptor.rl_file->lock_table[i].type = lock->l_type;
                                            descriptor.rl_file->lock_table[i].next_lock = -1;
                                            descriptor.rl_file->lock_table[i].owners_count = 1;
                                            descriptor.rl_file->lock_table[i].lock_owners[0] = lock_owner;
                                            current_lock.next_lock = i;
                                            info("rdlock added\n");
                                            pthread_mutex_unlock(&(descriptor.mutex));
                                            return 0;
                                        }
                                    }
                                    info("no more space for locks\n");
                                    pthread_mutex_unlock(&(descriptor.mutex));
                                    return -1; 
                                }
                            }
                        else if (current_lock.starting_offset>=lock->l_start+lock->l_len){//Tous les verrous suivants seront en dehors de l'intervalle car la liste est triée. 
                            break; 
                        }
                        else{
                            if (current_lock.type == F_RDLCK){
                                
                                if(current_lock.owners_count>1){
                                    info("already a rdlock");
                                    errno = EAGAIN;
                                    pthread_mutex_unlock(&(descriptor.mutex));
                                    return -1; 
                                }
                                else{//Si il n'y a qu'un owner et que c'est le thread courant, on pourra poser un verrou par dessus.
                                    if (current_lock.lock_owners[0].file_descriptor==descriptor.file_descriptor&&current_lock.lock_owners[0].thread_id==getpid()){
                                        if (current_lock.starting_offset <=lock->l_start){
                                            newStart=current_lock.starting_offset;       
                                        }
                                        if (current_lock.starting_offset+current_lock.length>=newEnd){
                                            newEnd=current_lock.starting_offset+current_lock.length;    
                                        }
                                        if(current_lock.next_lock!=-1){
                                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                            break; 
                                        }
                                        if (current_lock.next_lock==-1){
                                            hasnext = 0; 
                                        }
                                    }
                                    else{
                                        info("already a rdlock"); 
                                        errno = EAGAIN; 
                                        pthread_mutex_unlock(&(descriptor.mutex));
                                        return -1; 
                                    }
                                }
                                
                            }
                            else if (current_lock.type == F_WRLCK){
                                int courant = 0 ; 
                                for(size_t j = 0; j<current_lock.owners_count; j++){
                                    if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                                        //si le lock appartient au thread courant: 
                                        courant = 1; 
                                        if (current_lock.starting_offset <=lock->l_start){//Mise a jour des bornes pour préparer la fusion
                                            
                                            newStart=current_lock.starting_offset;       
                                        }
                                        if (current_lock.starting_offset+current_lock.length>=newEnd){
                                            newEnd=current_lock.starting_offset+current_lock.length;    
                                        }
                                        if(current_lock.next_lock!=-1){
                                            current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                            break; 
                                        }
                                        if (current_lock.next_lock==-1){
                                            break;
                                           
                                        }
                                        
                                    }
                                }
                                if( courant == 0 ){
                                    info("already a wrlock"); 
                                    errno=EAGAIN;
                                    pthread_mutex_unlock(&(descriptor.mutex));
                                    return -1; 
                                }
                                if(current_lock.next_lock!=-1){
                                    current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                    break; 
                                }
                                if (current_lock.next_lock==-1){
                                    hasnext = 0; 
                                }
                            }
                        }
                        
                       
                        
                    }
                    //Si on arrive ici, c'est qu'on a le droit de poser le verrou. Il faut cependant fusionner les verrous du même thread qui pourraient se trouver sur l'intervalle 
                  
                    //On enleve les verrous qui vont etre fusionnés 
                    current_lock=descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                    hasnext=1; 
                    ancienIndice[0] =-1;
                    ancienIndice[1] = descriptor.rl_file->first_lock;
                    while(hasnext){
                        if(current_lock.starting_offset+current_lock.length<lock->l_start){
                            if(current_lock.next_lock!=-1){
                                ancienIndice[0]= ancienIndice[1]; 
                                ancienIndice[1]=current_lock.next_lock; 
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                            }
                            else{
                                hasnext = 0; 
                            }
                        }
                        else if (current_lock.starting_offset>=lock->l_start+lock->l_len){
                            break;
                        }
                        else{
                            for(int j=0; j<current_lock.owners_count; j++){
                                //On enleve le proprietaire du lock
                                if (current_lock.lock_owners[j].thread_id == getpid() && current_lock.lock_owners[j].file_descriptor == descriptor.file_descriptor) {
                                    //On enleve le lock du thread courant
                                    current_lock.lock_owners[j].thread_id = -1, 
                                    current_lock.lock_owners[j].file_descriptor = -1; 
                                    for(int k = j; k<current_lock.owners_count-1; k++){
                                        current_lock.lock_owners[k] = current_lock.lock_owners[k+1];
                                    }
                                    current_lock.owners_count--;
                                    int index = check_empty_lock(&current_lock);
                                    if(index!=-3){
                                        if(ancienIndice[0]==-1){//si on doit supprimer le premier verrou dans la table
                                        if(index==-1){
                                            descriptor.rl_file->first_lock=-2; 
                                            hasnext=0; 
                                            break; 
                                        }
                                            descriptor.rl_file->first_lock = index; 
                                            ancienIndice[1]=index;
                                            current_lock = descriptor.rl_file->lock_table[index];
                                            break; 
                                        }
                                        if(index==-1){
                                        
                                            hasnext=0; 
                                            break; 
                                        }
                                        else{
                                        descriptor.rl_file->lock_table[ancienIndice[0]].next_lock=index; 
                                        ancienIndice[0]=ancienIndice[1];
                                        ancienIndice[1]=index; 
                                        current_lock = descriptor.rl_file->lock_table[index];
                                        break;
                                        }
                                    }
                                    if (current_lock.next_lock!=-1){
                                        current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                        ancienIndice[0]=ancienIndice[1];
                                        ancienIndice[1]=current_lock.next_lock;
                                        break; 
                                    }
                                    else{
                                        hasnext = 0; 
                                        break; 
                                    }
                                }
                            }    
                            if (current_lock.next_lock!=-1){
                                current_lock = descriptor.rl_file->lock_table[current_lock.next_lock];
                                ancienIndice[0]=ancienIndice[1];
                                ancienIndice[1]=current_lock.next_lock;
                                break; 
                            }
                            else{
                                hasnext = 0; 
                                break; 
                            }
                        }
                    }
    
                    if (descriptor.rl_file->first_lock==-1){//Si on a supprimé tous les verrous sur le fichier : 
                        descriptor.rl_file->first_lock=0;
                        descriptor.rl_file->lock_table[0].length= newEnd-newStart; 
                        descriptor.rl_file->lock_table[0].lock_owners[0]=lock_owner;
                        descriptor.rl_file->lock_table[0].next_lock=-1; 
                        descriptor.rl_file->lock_table[0].owners_count=1; 
                        descriptor.rl_file->lock_table[0].starting_offset=newStart;
                        descriptor.rl_file->lock_table[0].type=F_WRLCK;
                        info("rdlock added"); 
                        pthread_mutex_unlock(&(descriptor.mutex));
                        return 0; 
                    }
                    current_lock=descriptor.rl_file->lock_table[descriptor.rl_file->first_lock];
                    hasnext=1; 
                    ancienIndice[0]=-1; 
                    ancienIndice[1]=descriptor.rl_file->first_lock;
                    while (hasnext){
                        if(current_lock.starting_offset<newStart){
                            if (current_lock.next_lock==-1){
                                
                                return addLock(&current_lock, ancienIndice[1],current_lock.next_lock, newStart, newEnd-newStart, F_WRLCK, getpid(), &descriptor, lock_owner);
                               
                            }
                            else{
                                if(descriptor.rl_file->lock_table[current_lock.next_lock].starting_offset>=newStart){
                                    return addLock(&current_lock, ancienIndice[1],current_lock.next_lock, newStart, newEnd-newStart, F_WRLCK, getpid(), &descriptor, lock_owner);
                                    
                                }
                                else{
                                    ancienIndice[0]=ancienIndice[1];
                                    ancienIndice[1]=current_lock.next_lock;
                                    current_lock=descriptor.rl_file->lock_table[current_lock.next_lock];
                                }
                            }
                        }
                        else{
                            return addLock(&current_lock,-2, descriptor.rl_file->first_lock, newStart, newEnd-newStart, F_WRLCK, getpid(), &descriptor, lock_owner);
                            
                        }
                    }
                break;
                case F_UNLCK: /** todo vérifier que ça marche */
                    info("requesting an unlock\n");	// DEBUG
                    print_flock(lock);	// DEBUG
                    // TODO
                    error("owner not found\n");
                    pthread_mutex_unlock(&(descriptor.mutex));
                    return -1;  
                break;
            }
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