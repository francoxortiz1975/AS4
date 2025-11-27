/*
 *------------
 * This code is provided solely for the personal and private use of
 * students taking the CSC369H5 course at the University of Toronto.
 * Copying for purposes other than this use is expressly prohibited.
 * All forms of distribution of this code, whether as given or with
 * any changes, are expressly prohibited.
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2025 MCS @ UTM
 * -------------
 */

/**
 * TODO: Make sure to add all necessary includes here
 */

#include "e2fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>


// Global variables
extern unsigned char* disk; 
extern struct ext2_super_block *sb;
extern struct ext2_group_desc* gd;
extern unsigned char* inode_table;
extern struct ext2_inode* root_inode;

extern pthread_mutex_t inode_locks[32];
//extern char reference_counts[32];
extern pthread_mutex_t sb_lock;
extern pthread_mutex_t gd_lock;


static const int DIR_ENTRY_MIN_SIZE = 8;





/**
 * This helper function returns the number of free blocks on the drive.
 * 
 * Lock Info: It assumes there is a lock on gd.
 */
static int ex2_find_free_blocks_count() {
    unsigned char* block_bitmap = (disk + (1024 * gd->bg_block_bitmap));
    int free_blocks = 0;
    for (int i = 0; i < sb->s_blocks_count / 8; i++) {
        char block_byte = *(block_bitmap + i);
        // Check each bit
        for (char j = 0; j < 8; j++) {
            char mask = 1 << j;

            // if it's free
            if ((mask & block_byte) == 0) {
                free_blocks++;
            }
        }
    }
    return free_blocks;
}

/**
 * This function searches the block bitmap and returns the index of the first free non-reserved entry.
 * It also marks the bitmap entry as claimed.
 * If none is found, returns -1.
 * 
 * Lock Info: It assumes the sb and gd locks are claimed.
 */
int ex2_search_free_block_bitmap() {
    unsigned char* block_bitmap = (disk + (1024 * gd->bg_block_bitmap));

    // Check each byte
    for (int i = 0; i < sb->s_inodes_count / 8; i++) {
        char block_byte = *(block_bitmap + i);
        // Check each bit
        for (char j = 0; j < 8; j++) {
            int blockno = i * 8 + j;
            char mask = 1 << j;

            // if it's free
            if ((mask & block_byte) == 0) {
                *(block_bitmap + i) |= mask;

                // increment the superblock and gd counts
                sb->s_free_blocks_count--;

                gd->bg_free_blocks_count--;

                // return the block
                return blockno + 1;
                
            }
        }
    }
    // Nothing found, return -1
    return -1;
}


/**
 * This function searches the inode bitmap and returns the index to the first free non-reserved inode.
 * It also marks the bitmap entry as claimed.
 * If none is found, returns -1.
 * 
 * Lock Info: It assumes the sb and gd locks are already held.
 */
static int ex2_search_free_inode_bitmap() {

    // Get the inode bitmap, table from the group descriptor
    unsigned char* inode_bitmap = (disk + (1024 * gd->bg_inode_bitmap));

    // Check each byte
    for (int i = 0; i < sb->s_inodes_count / 8; i++) {
        char inode_byte = *(inode_bitmap + i);
        // Check each bit        
        for (char j = 0; j < 8; j++) {
            int inodeno = i * 8 + j;
            // Don't choose reserved inodes
            if (inodeno < EXT2_GOOD_OLD_FIRST_INO) continue;
            char mask = 1 << j;
            // If it's free
            if ((mask & inode_byte) == 0) {
                // Mark it as claimed
                *(inode_bitmap + i) |= mask;

                // increment the superblock and gd counts
                sb->s_free_inodes_count--;
                gd->bg_free_inodes_count--;

                // Return the pointer
                return inodeno;
            }
        }
    }

    // Nothing found, return -1
    return -1;
}

/**
 * This function unmarks the index in the inode bitmap.
 * 
 * Lock Info: This assumes a write lock is held for the sb and gd.
 */
static void ex2_unmark_inode_bitmap(int index) {

    // Get the inode bitmap, table from the group descriptor
    unsigned char* inode_bitmap = (disk + (1024 * gd->bg_inode_bitmap));

    // Go directly to the index byte
    unsigned char* inode_byte = (inode_bitmap + (index / 8));

    char bit = 1 << (index % 8);
    // Unclaim the bit
    *inode_byte &= ~bit;

    // Decrement the superblock and gd counts
    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
}

/**
 * This function aligns to 4 bytes
 */
static int align_4_bytes(int num) {
    if (num % 4 == 0) return num;
    else return num + 4 - (num % 4);
}

/**
 * This helper function searches and allocates a free directory entry in a directory inode
 * 
 * Lock Info: It expects that a write lock is held for the folder.
 * It also assumes the sb and gd locks are claimed (if there needs to be a new node)
 */
struct ext2_dir_entry* ex2_search_free_dir_entry(struct ext2_inode* folder, char* name, unsigned int inode) {

    int size_acc = 0;
    
    // If initializing with "."
    if (folder->i_size == 0) {
        struct ext2_dir_entry* new_entry = (struct ext2_dir_entry*)(disk + (1024 * folder->i_block[0]));
        // truncate the file
        memset(new_entry, 0, 1024);

        new_entry->inode = inode + 1;
        new_entry->rec_len = EXT2_BLOCK_SIZE;
        new_entry->name_len = strlen(name);
        strcpy(new_entry->name, name);
        new_entry->file_type = 0; // Temp init
        folder->i_size = 1024;
        return new_entry;
    }
    // We use the assumption that there will only be direct blocks for directories
    // Thus, only 12 blocks
    for (int i = 0; i < 12; i++) {
        int block_num = folder->i_block[i];
        

        struct ext2_dir_entry* entry;
        // Loop through the block
        do {
            entry = (struct ext2_dir_entry *)(disk + (1024 * block_num) + size_acc);
            size_acc += entry->rec_len;
        } 
        while (size_acc % 1024);

        if (size_acc >= folder->i_size) {
            struct ext2_dir_entry* new_entry;
            
            int new_entry_actual_size = DIR_ENTRY_MIN_SIZE + align_4_bytes(strlen(name));
            int last_entry_actual_size = DIR_ENTRY_MIN_SIZE + align_4_bytes(entry->name_len);
            // Check if there's enough space for the new directory entry to go in the current block
            if (entry->rec_len - last_entry_actual_size >= new_entry_actual_size) {
                char* next_block_boundary = (char*)entry + entry->rec_len;
                entry->rec_len = last_entry_actual_size;
                new_entry = (struct ext2_dir_entry *)((char*)entry + entry->rec_len);
                new_entry->rec_len = next_block_boundary - (char*)new_entry;
            } else {
                // You have to allocate a new block first.
                int new_block_no = ex2_search_free_block_bitmap();

                // TODO check to see if this is enough for error handling/propagation
                if (new_block_no == -1) return NULL;

                char* new_block = (char*)(disk + new_block_no * EXT2_BLOCK_SIZE);

                // Edit inode data
                folder->i_block[i+1] = new_block_no;
                folder->i_blocks += 2;

                // In the new block, truncate the data to prevent corruption
                memset(new_block, 0, 1024);

                // Declare new entry
                new_entry = (struct ext2_dir_entry *)new_block;
                new_entry->rec_len = EXT2_BLOCK_SIZE;

                folder->i_size += EXT2_BLOCK_SIZE;
            }

            new_entry->inode = inode + 1; // i think?
            new_entry->name_len = strlen(name);
            strcpy(new_entry->name, name);
            new_entry->file_type = 0; // Init to 0 at first

            return new_entry;
        } 


    }
    fprintf(stderr, "ex2_search_free_dir_entry: Looped through all 12 direct pointers, should not happen\n");
    return NULL;
}

/**
 * This function frees a given directory entry.
 */
void ex2_free_dir_entry(struct ext2_dir_entry* entry) {
    entry->inode = 0;
}

/**
 * This function gets an inode number from the directory entry number and resolves
 * it into a pointer to an ext2_inode.
 */
struct ext2_inode* resolve_inode_number(unsigned int inodeno) {
    // This is fine without a lock because the gd's bg_inode_table should not change 
    unsigned char* inode_table = (disk + (1024 * gd->bg_inode_table));
    return (struct ext2_inode*)(inode_table + sizeof(struct ext2_inode) * inodeno);
}

/**
 * This function attempts to find a directory entry given a current directory inode
 * and a name. If successful, it will return the entry. If not, it will return NULL.
 * 
 * Lock info: A read (or write) lock should be held on the directory before calling this function.
 */
struct ext2_dir_entry* e2_find_dir_entry(struct ext2_inode* directory, char* name, int name_length) {
    if (name_length == 0) return NULL;
    if (directory->i_mode >> 12 != EXT2_S_IFDIR >> 12) {
        fprintf(stderr, "e2_find_dir_entry: This was not called with a directory inode!\n");
        exit(1);
    }
    // From the inode, get the direct blocks
    // We can assume that directories only have direct blocks
    for (int i = 0; i < 12; i++) {
        int block_num = directory->i_block[i];
        // Since blocks gives us double the actual count
        if (i + 1 > directory->i_blocks / 2) break;

        // Iterate through the linked list of entries
        struct ext2_dir_entry* entry = (struct ext2_dir_entry*)(disk + (1024 * block_num));
        while (1) {

            // If the name matches
            if (
                entry->inode != 0 &&
                name_length == entry->name_len &&
                strncmp(name, entry->name, name_length) == 0
            ) {
                return entry;
            }

            // Go to the next entry
            entry = (struct ext2_dir_entry *)((char*)entry + entry->rec_len);
            // Break if at the end
            if (((char*)entry - (char*)disk) % 1024 == 0) {
                break;
            }
        }
    }
    return NULL;
}

/**
 * This function implements a path-walk, given an absolute path string.
 * It returns an ex2_dir_wrapper, a struct containing:
 * - a pointer to a directory entry (file, symlink, or directory) which is NULL on failure
 *   It can also be NULL if errcode returns 1 and the path is from the root dir, like with "/1"
 * - an int with parent's inode index. 
 * - an error code which is 0 on success, 1 if the last item does not exist, and an errno value on error. 
 *   If errcode is 0, the pointer points to the file entry itself. If errcode is 1, it points to the parent directory entry 
 *   (which can be NULL if parent is root)
 * Possible errors:
 * - EFAULT if the path is not absolute
 * - EINVAL if there is a path with 0 length item like "/1//3"
 * - ENOENT if there is an item that doesn't exist
 * Note: If it is a nonexistent file in the root directory, like "/foo", it will return a null directory entry.
 * 
 * Lock Info: It returns holding the lock of the parent directory and the file (if it exists).
 * If there is an error, the function returns holding no locks.
 */
struct ex2_dir_wrapper e2_path_walk_absolute(const char* path) {
    struct ex2_dir_wrapper wrap;
    // Check that the first character of the string is / (so it's properly absolute)
    if (path[0] != '/') {
        wrap.errcode = EFAULT;
        return wrap;
    }

    // Since it does start with /, start with inode 2 (the / directory)
    // Lock the root directory

    // Start at the first directory number
    char* previous_slash = (char*)path;
    char* next_slash = strchr(previous_slash + 1, '/');
    struct ext2_inode* current_dir_inode = root_inode;
    struct ext2_dir_entry* current_dir_entry = NULL;
    pthread_mutex_t* prev_lock = &inode_locks[1];
    pthread_mutex_t* new_lock = prev_lock;

    if (next_slash == NULL) {
        // It is the root directory
        if (*(previous_slash + 1) == '\0') {
            wrap.entry = NULL;
            wrap.parent_inode = -1;
            wrap.errcode = 0;
            wrap.last_token = previous_slash + 1;

            pthread_mutex_lock(prev_lock);
            return wrap;
        } 
        // It's in the root directory
        else {
            // get a lock for this
            pthread_mutex_lock(prev_lock);
            // try to see if it exists
            struct ext2_dir_entry* found_entry = e2_find_dir_entry(root_inode, previous_slash + 1, strlen(previous_slash + 1));
            wrap.parent_inode = 1;

            if (found_entry != NULL) {
                // Lock the found entry since it exists
                pthread_mutex_lock(&inode_locks[found_entry->inode - 1]);
    
                wrap.entry = found_entry;
                wrap.errcode = 0;
                wrap.last_token = previous_slash + 1;
            }
            else {
                wrap.entry = NULL;
                wrap.errcode = 1;
                wrap.last_token = previous_slash + 1;
            }

            return wrap;    
        }
    }
    // Use a read or write lock depending on if the slash is the last or not
    pthread_mutex_lock(prev_lock);

    // While in this loop, all path items should be directories
    while (next_slash != NULL && *(next_slash + 1) != '\0') {
        
        // Evaluate the current "token"
        int token_length = next_slash - previous_slash - 1;
        if (token_length == 0) {
            wrap.errcode = EINVAL;
            pthread_mutex_unlock(prev_lock);
            return wrap;
        }

        // call e2_find_dir_entry
        current_dir_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, token_length);
        // Check that the entry exists
        if (current_dir_entry == NULL) {
            wrap.errcode = ENOENT;
            pthread_mutex_unlock(prev_lock);
            return wrap;
        }

        // now you have the new inode, lock it
        new_lock = &inode_locks[current_dir_entry->inode - 1];
        pthread_mutex_lock(new_lock);
        // then, unlock the old inode
        pthread_mutex_unlock(prev_lock);
        prev_lock = new_lock;

        current_dir_inode = resolve_inode_number(current_dir_entry->inode - 1);
        
        
        // Get the type of the entry we got
        char file_type;
        switch (current_dir_entry->file_type)
        {
        case 1:
            file_type = 'f';
            break;
        case 2:
            file_type = 'd';
            break;
        case 7:
            file_type = 's';
            break;
        default:
            file_type = '?';
            break;
        }

        // In our assumptions, it states that a path will not include symlinks in the middle.
        // Check that it's the correct file type (directory)
        if (file_type != 'd') {
            wrap.errcode = ENOENT;
            pthread_mutex_unlock(prev_lock);
            return wrap;
        }

        // Continue the loop
        previous_slash = next_slash;
        next_slash = strchr(previous_slash + 1, '/');    
    }
    // Now, new_lock is acquired

    // Paths could end like /1/2/3 or /1/2/3/. 
    // If we have a path like /1/2/3, 3 will go here
    if (next_slash == NULL) {
        // Try to return the item if found.
        struct ext2_dir_entry* found_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, strlen(previous_slash + 1));
        wrap.parent_inode = current_dir_entry->inode - 1;

        if (found_entry != NULL) {
            // If it exists, lock it
            pthread_mutex_lock(&inode_locks[found_entry->inode - 1]);
            wrap.entry = found_entry;
            wrap.errcode = 0;
            wrap.last_token = previous_slash + 1;
        }
        else {
            wrap.entry = current_dir_entry;
            wrap.errcode = 1;
            wrap.last_token = previous_slash + 1;
        }
        // return with locks held
        return wrap;
    } 
    // If we have a path like /1/2/3/, return the directory if it is one
    else {
        // Find the last item and check that it's a directory
        struct ext2_dir_entry* found_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, strlen(previous_slash + 1) - 1);
        wrap.parent_inode = (current_dir_entry != NULL) ? current_dir_entry->inode - 1 : -1;

        if (found_entry != NULL && found_entry->file_type == 2) {
            // If it exists, lock it
            pthread_mutex_lock(&inode_locks[found_entry->inode - 1]);
            wrap.entry = found_entry;
            wrap.errcode = 0;
            wrap.last_token = previous_slash + 1;
            return wrap;
        } else {
            // This depends on the function but it's safer to return success and let them deal with it
            wrap.entry = current_dir_entry;
            wrap.errcode = 1;
            wrap.last_token = previous_slash + 1;
            return wrap;
        }
    }
}

/**
 * Helper function to create a file with no specified type, and a name.
 * Returns the directory entry on success. 
 * Returns NULL on failure (if there is no space left).
 * If parent is NULL, it refers to the root directory.
 * 
 * Lock info: It expects a lock to be held on the parent dir entry's inode. 
 * It also acquires a write lock for the new file. (Releases if NULL is returned)
 * Lastly, it acquires the sb and gd locks, which must be released after (Even if NULL).
 */
struct ext2_dir_entry* e2_create_file_setup(struct ext2_dir_entry* parent, char* name, int blocks_needed) {
    pthread_mutex_lock(&sb_lock);
    pthread_mutex_lock(&gd_lock);

    // Find/allocate a new inode for the file
    int inodeno = ex2_search_free_inode_bitmap();
    if (inodeno == -1) return NULL;
    
    struct ext2_inode* new_inode = resolve_inode_number(inodeno);
    struct ext2_inode* parent_inode;
    if (parent == NULL) {
        parent_inode = root_inode;
    } else {
        parent_inode = resolve_inode_number(parent->inode - 1);
    }

    // Initialize the inode (partially)
    // Obtain a write lock on the inode
    pthread_mutex_lock(&inode_locks[inodeno]);


    // Temporarily initialize i_mode, i_size, i_dtime
    new_inode->i_mode = 0;
    new_inode->i_size = 0;
    new_inode->i_dtime = 0;

    // Temporarily initialize i_block
    memset(new_inode->i_block, 0, 15 * sizeof(unsigned int));
    memset(new_inode->extra, 0, 3 * sizeof(unsigned int));

    new_inode->i_uid = 0;
    new_inode->i_gid = 0;

    new_inode->i_flags = 0;
    new_inode->osd1 = 0;
    new_inode->i_generation = 0;
    new_inode->i_file_acl = 0;
    new_inode->i_dir_acl = 0;
    new_inode->i_faddr = 0;


    // Set links_count to 1 because it creates a file in a directory
    new_inode->i_links_count = 1;

    // Do NOT allocate data block(s) yet

    // update the parent directory with the extra directory entry
    // by going to the last spot in the linked list
    struct ext2_dir_entry* new_dir_entry = ex2_search_free_dir_entry(parent_inode, name, inodeno);
    // If there is no space for the new entry (if in a new block), undo inode mapping and return.
    if (new_dir_entry == NULL) {
        ex2_unmark_inode_bitmap(inodeno);

        pthread_mutex_unlock(&inode_locks[inodeno]);
        return NULL;
    }

    // Check that there are enough free blocks in the system
    int free_blocks = ex2_find_free_blocks_count();

    // If there is no space left, undo the new inode mapping and dir entry and return
    if (free_blocks < blocks_needed) {
        ex2_unmark_inode_bitmap(inodeno);

        ex2_free_dir_entry(new_dir_entry);
        pthread_mutex_unlock(&inode_locks[inodeno]);
        return NULL;
    }

    // superblock/group descriptors have been updated in helpers, so no work necessary here
    return new_dir_entry;

}

//-------------------ADDED FOR CP--------------------------//

/**
 * Helper for file_init, writes buffer to block.
 * 
 * Lock Info: N/A
 */
void write_block_data(int block_num, char *data) {
    //offset of the disk + block
    unsigned char *block_location = disk + (block_num * 1024);
    
    //copy data to block location
    memcpy(block_location, data, 1024);
}
/**
 * Helper that initializes a file. It also sets the inode and dir_entry's
 * file type.
 * 
 * Lock Info: It assumes the sb and gd locks are claimed, and unlocks them.
 * It also expects a lock on the file_entry's inode, and releases it.
 */
int file_init(struct ext2_dir_entry *file_entry, const char *source_path) {
    //get file in read + binary op
    FILE *source = fopen(source_path, "rb");

    if (!source) {
        pthread_mutex_unlock(&sb_lock);
        pthread_mutex_unlock(&gd_lock);   
        pthread_mutex_unlock(&inode_locks[file_entry->inode - 1]); 
        return ENOENT;  // error if not source
    }

    // Modify the file_entry's file_type
    file_entry->file_type = EXT2_FT_REG_FILE;
    
    //get size, we move to the end of file
    fseek(source, 0, SEEK_END);
    int file_size = ftell(source);
    //once we get size with ftell, go back
    rewind(source);

    //extract the file inode in entry already
    struct ext2_inode *file_inode = resolve_inode_number(file_entry->inode - 1);

    //compute how many blocks we need
    int blocks_needed = (file_size + 1023) / 1024;

    //we can assume file is not to big to require more than 13 blocks, 12 direct and first indirect
    unsigned int *indirect_block = NULL;

    for (int i = 0; i < blocks_needed; i++) {
        int free_block = ex2_search_free_block_bitmap();

        if (i < 12) {
            // Bloques directos (0-11)
            file_inode->i_block[i] = free_block;
        } 
        else {
            //case indirect block
            if (i == 12) {
                //create a block inode
                int indirect_block_num = ex2_search_free_block_bitmap();
                file_inode->i_block[12] = indirect_block_num;
                
                //pointer to it in disk
                indirect_block = (unsigned int*)(disk + (indirect_block_num * 1024));
                
                //clean the indirect block (1024 bytes)
                memset(indirect_block, 0, 1024);
            }
            
            // Guardar número de bloque en el bloque indirecto
            indirect_block[i - 12] = free_block;
        }

        //1kb buffer
        char buffer[1024];

        //read from 1 to 1024 bytes from source and save it into buffer.
        size_t bytes_read = fread(buffer, 1, 1024, source);

        //if last, fill with 0s.
        if (bytes_read < 1024) {
            memset(buffer + bytes_read, 0, 1024 - bytes_read);
        }

        //normal blocks, sets of 1024 buffer
        write_block_data(free_block, buffer);
    }
    pthread_mutex_unlock(&sb_lock);
    pthread_mutex_unlock(&gd_lock);

     //type and permission update.
     //TYPE: REGULAR FILE, 
     //PERMISSION: 6: rw owner, r, r 
    // REVIEW: change i_mode, since we don't worry about permissions
    file_inode->i_mode &= ~0xF000;
    file_inode->i_mode |= EXT2_S_IFREG;

    file_inode->i_size = file_size; //size of file in bytes
    file_inode->i_blocks = blocks_needed * 2;  //2 inodes of 512 bytes for 1 block of 1024

    if (blocks_needed > 12) {
        file_inode->i_blocks += 2;  // +1 bloque indirecto × 2 sectores
    }
    fclose(source);
    pthread_mutex_unlock(&inode_locks[file_entry->inode - 1]);
    return 0;
}

/**
 * This function unmarks the selected block from the bitmap.
 * 
 * Lock Info: It expects the sb and gd locks to be held.
 */
static void ex2_unmark_block_bitmap(int block_num) {
    //same logic as unmark inode bitmap
    //positioon of bitmap
    unsigned char* block_bitmap = (disk + (1024 * gd->bg_block_bitmap));
    
    //
    int bit_position = block_num - 1;  //to get the right index

    //depending on the bit position, get the BLOCKBYTE where the block is.
    unsigned char* block_byte = (block_bitmap + (bit_position / 8));
    //then get the bit IN this BLOCKBYTE with mask
    char bit = 1 << (bit_position % 8);
    
    //free byte: AND Operation with the actual byte, and the mask of the bit  NEGATED (so, it ill be 0, to be cleaned with AND
    *block_byte &= ~bit;
    
    //counters update
    sb->s_free_blocks_count++;
    gd->bg_free_blocks_count++;
}

/**
 * This helper frees unneeded blocks.
 * 
 * Lock Info: Expects the sb and gd locks to be held.
 */
void free_blocks(struct ext2_inode *inode, int new_blocks, int old_blocks) {
    //free blocks not needed
    for (int i = new_blocks; i < old_blocks; i++) {
        int block_to_free;
        
        //indirect
        if (i < 12) {
            block_to_free = inode->i_block[i];
            inode->i_block[i] = 0;  // Limpiar referencia
        } 
        //direct
        else {
            //block
            unsigned int *indirect = (unsigned int*)(disk + inode->i_block[12] * 1024);
            block_to_free = indirect[i - 12];
            indirect[i - 12] = 0;
        }
        
        //unmark in bitmap
        ex2_unmark_block_bitmap(block_to_free);
    }
    
    //if indirect block no more needed
    if (new_blocks <= 12 && old_blocks > 12) {
        ex2_unmark_block_bitmap(inode->i_block[12]);
        inode->i_block[12] = 0;
    }
}
/**
 * This helper assigns blocks.
 * 
 * Lock Info: It assumes the sb and gd locks are claimed.
 */
void assign_blocks(struct ext2_inode *inode, int old_blocks, int new_blocks_needed) {
    unsigned int *indirect_block = NULL;
    
    //indirect block case
    if (old_blocks > 12) {
        indirect_block = (unsigned int*)(disk + (inode->i_block[12] * 1024));
    }
    
    //from old blocks, to new blocks, free.
    for (int i = old_blocks; i < new_blocks_needed; i++) {
        int free_block = ex2_search_free_block_bitmap();
        
        if (i < 12) {
            //direct 
            inode->i_block[i] = free_block;
        }
        else {
            //indirect
            if (i == 12) {
                //create indirect block
                int indirect_block_num = ex2_search_free_block_bitmap();
                inode->i_block[12] = indirect_block_num;
                
                //indirect
                indirect_block = (unsigned int*)(disk + (indirect_block_num * 1024));
                
                //free set
                memset(indirect_block, 0, 1024);
            }
            
            //save free blocks in indirect block array, with its index.
            indirect_block[i - 12] = free_block;
        }
    }
}
/**
 * Helper that copies a file to the inode blocks.
 * 
 * Lock Info: N/A
 */
void copy_to_file(struct ext2_inode *inode, FILE *source, int blocks_needed) {

    unsigned int *indirect_block = NULL;
    
    //if indirect block needed, get pointer
    if (blocks_needed > 12) {
        indirect_block = (unsigned int*)(disk + (inode->i_block[12] * 1024));
    }
    
    //copy data for each block
    for (int i = 0; i < blocks_needed; i++) {
        int target_block;
        
        //direct
        if (i < 12) {
            //block destiny
            target_block = inode->i_block[i];
        } else {
            //indirect
            target_block = indirect_block[i - 12];
        }
        
        //create a buffer to READ
        char buffer[1024];
        //read from 1 to 1024 bytes from source and copy it to buffer
        size_t bytes_read = fread(buffer, 1, 1024, source);
        
        //if bytes incomplete, fill with 0
        if (bytes_read < 1024) {
            memset(buffer + bytes_read, 0, 1024 - bytes_read);
        }
        
        //write int the block in mem
        write_block_data(target_block, buffer);
    }
}


/**
 * Helper that overwrites a file.
 * 
 * Lock info: Locks and unlocks the sb and gd locks.
 */
int file_overwrite(struct ext2_dir_entry *existing_entry, const char *source_path) {
    
    // source path
    FILE *source = fopen(source_path, "rb");
    if (!source) return ENOENT;
    
    fseek(source, 0, SEEK_END);
    int new_file_size = ftell(source);
    rewind(source);
    
    //existing inode
    struct ext2_inode *existing_inode = resolve_inode_number(existing_entry->inode - 1);
    
    // compute blocks
    int old_blocks = existing_inode->i_blocks / 2;
    int new_blocks_needed = (new_file_size + 1023) / 1024;

    pthread_mutex_lock(&sb_lock);
    pthread_mutex_lock(&gd_lock);

    //adjust if more blocks needed
    if (new_blocks_needed < old_blocks) {
        //free some blocks
        free_blocks(existing_inode, new_blocks_needed, old_blocks);
    }
    else if (new_blocks_needed > old_blocks) {
        //assign more block
        assign_blocks(existing_inode, old_blocks, new_blocks_needed);
    }
    
    pthread_mutex_unlock(&sb_lock);
    pthread_mutex_unlock(&gd_lock);

    //copy data
    copy_to_file(existing_inode, source, new_blocks_needed);
    
    //inode new size
    existing_inode->i_size = new_file_size;
    //inode more info.
    existing_inode->i_blocks = new_blocks_needed * 2;
    if (new_blocks_needed > 12) {
        existing_inode->i_blocks += 2;  // Bloque indirecto
    }
    
    fclose(source);
    return 0;
}


int file_exists(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;  
}

/**
 * Helper that copies into a directory.
 * 
 * Lock info: It expects a lock to be held on the dir_entry's inode.
 * It also locks and unlocks the sb and gd locks.
 */
int copy_into_directory(struct ext2_dir_entry *dir_entry, const char *src) {
    //GET Name of SRC
    const char *filename = strrchr(src, '/');
    if (filename) {
        filename++; //skip /
    } else {
        filename = src; //use all filename, NO  /
    }
    
    //get directory inode
    struct ext2_inode *parent_inode = resolve_inode_number(dir_entry->inode - 1);
    
    //check if already exists 
    struct ext2_dir_entry *existing_file = e2_find_dir_entry(parent_inode, (char*)filename, strlen(filename));
    
    if (existing_file != NULL) {
        //if EXISTS, overwrite
        return file_overwrite(existing_file, src);
    } else {
        //DOESNT EXISTS, 
        //check file
        FILE *source = fopen(src, "rb");
        if (!source) return ENOENT;
        
        //to get the size for the blocks needed, we do fseek to the end
        fseek(source, 0, SEEK_END);
        int file_size = ftell(source);
        fclose(source);
        
         //compute blocks needed

        int blocks_needed = (file_size + 1023) / 1024;
        
        //setup helper
        struct ext2_dir_entry *new_entry = e2_create_file_setup(dir_entry, (char*)filename, blocks_needed);
        if (!new_entry) {
            pthread_mutex_unlock(&sb_lock);
            pthread_mutex_unlock(&gd_lock);    
            return ENOSPC;
        }
        
        //init helper
        return file_init(new_entry, src);
    }
}
/**
 * This helper creates a new file.
 * 
 * Lock Info: It expects a lock held on the parent_entry's inode.
 * It acquires a write lock for the new file.
 * It acquires and releases the sb and gd locks.
 */
int create_new_file(struct ext2_dir_entry *parent_entry, const char *filename, const char *src) {
    //DOESNT EXISTS, 
    //check file
    FILE *source = fopen(src, "rb");
    if (!source) return ENOENT;
    
    //to get the size for the blocks needed, we do fseek to the end
    fseek(source, 0, SEEK_END);
    int file_size = ftell(source);
    fclose(source);
    
    //compute blocks needed
    int blocks_needed = (file_size + 1023) / 1024;
    
    //same tehcnique to get the size of the filename
    char clean_filename[256];
    strcpy(clean_filename, filename);
    int len = strlen(clean_filename);

    //IF TRAILING SLASH exists, not include it on the name.s
    if (len > 0 && clean_filename[len-1] == '/') {
        clean_filename[len-1] = '\0';
    }
    
    //setup helper
    struct ext2_dir_entry *new_entry = e2_create_file_setup(parent_entry, clean_filename, blocks_needed);
    if (!new_entry) {
        pthread_mutex_unlock(&sb_lock);
        pthread_mutex_unlock(&gd_lock);
        return ENOSPC;
    }
    
    //init helper
    return file_init(new_entry, src);
}