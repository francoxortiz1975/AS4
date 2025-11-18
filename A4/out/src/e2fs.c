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


// Global variables
extern unsigned char* disk; 
extern struct ext2_super_block *sb;
extern struct ext2_group_desc* gd;
extern unsigned char* inode_table;
extern struct ext2_inode* root_inode;


static const int DIR_ENTRY_MIN_SIZE = 8;


/**
 * This helper function returns the number of free blocks on the drive.
 */
static int ex2_find_free_blocks_count() {
    unsigned char* block_bitmap = (disk + (1024 * gd->bg_block_bitmap));
    int free_blocks = 0;
    for (int i = 0; i < sb->s_inodes_count / 8; i++) {
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
                sb->s_blocks_count++;
                sb->s_free_blocks_count--;

                gd->bg_free_blocks_count--;

                // return the block
                return blockno;
                
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
                sb->s_inodes_count++;
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


static void ex2_unmark_inode_bitmap(int index) {
    // Get the inode bitmap, table from the group descriptor
    unsigned char* inode_bitmap = (disk + (1024 * gd->bg_inode_bitmap));

    // Go directly to the index byte
    unsigned char* inode_byte = (inode_bitmap + (index / 8));

    char bit = 1 << (index % 8);
    // Unclaim the bit
    *inode_byte &= ~bit;

    // Decrement the superblock and gd counts
    sb->s_inodes_count--;
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
 */
struct ext2_dir_entry* ex2_search_free_dir_entry(struct ext2_inode* folder, char* name, unsigned int inode) {

    int size_acc = 0;
    
    // If initializing with "."
    if (folder->i_size == 0) {
        struct ext2_dir_entry* new_entry = (struct ext2_dir_entry*)(disk + (1024 * folder->i_block[0]));
        new_entry->inode = inode;
        new_entry->rec_len = EXT2_BLOCK_SIZE;
        new_entry->name_len = strlen(name);
        strcpy(new_entry->name, name);
        new_entry->file_type = 0; // Temp init
        return new_entry;
    }
    // We use the assumption that there will only be direct blocks for directories
    // Thus, only 12 blocks
    for (int i = 0; i < 12; i++) {
        int block_num = folder->i_block[i];
        

        struct ext2_dir_entry* entry = (struct ext2_dir_entry*)(disk + (1024 * block_num));
        // Loop through the block
        do {
            // If size_acc >= folder->i_size
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
                    int new_block = ex2_search_free_block_bitmap();

                    // TODO check to see if this is enough for error handling/propagation
                    if (new_block == -1) return NULL;

                    // Edit inode data
                    folder->i_block[i+1] = new_block;
                    folder->i_blocks += 2;

                    // Declare new entry
                    new_entry = (struct ext2_dir_entry *)(disk + block_num * EXT2_BLOCK_SIZE);
                    new_entry->rec_len = EXT2_BLOCK_SIZE;

                    folder->i_size += EXT2_BLOCK_SIZE;
                }

                new_entry->inode = inode;
                new_entry->name_len = strlen(name);
                strcpy(new_entry->name, name);
                new_entry->file_type = 0; // Init to 0 at first

                return new_entry;
            } 

            size_acc += entry->rec_len;
            entry = (struct ext2_dir_entry *)((char*)entry + entry->rec_len);
        } 
        while (((char*)entry - (char*)disk) % 1024);
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
    unsigned char* inode_table = (disk + (1024 * gd->bg_inode_table));
    return (struct ext2_inode*)(inode_table + sizeof(struct ext2_inode) * inodeno);
}

/**
 * This function attempts to find a directory entry given a current directory inode
 * and a name. If successful, it will return the entry. If not, it will return NULL.
 */
static struct ext2_dir_entry* e2_find_dir_entry(struct ext2_inode* directory, char* name, int name_length) {
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
 * - an error code which is 0 on success, 1 if the last item does not exist, and an errno value on error. 
 *   If errcode is 0, the pointer points to the file entry itself. If errcode is 1, it points to the parent directory entry 
 *   (which can be NULL if parent is root)
 * Possible errors:
 * - EFAULT if the path is not absolute
 * - EINVAL if there is a path with 0 length item like "/1//3"
 * - ENOENT if there is an item that doesn't exist
 */
struct ex2_dir_wrapper e2_path_walk_absolute(const char* path) {
    struct ex2_dir_wrapper wrap;
    // Check that the first character of the string is / (so it's properly absolute)
    if (path[0] != '/') {
        wrap.errcode = -EFAULT;
        return wrap;
    }

    // Since it does start with /, start with inode 2 (the / directory)


    // Start at the first directory number
    char* previous_slash = (char*)path;
    char* next_slash = strchr(previous_slash + 1, '/');
    struct ext2_inode* current_dir_inode = root_inode;
    struct ext2_dir_entry* current_dir_entry = NULL;

    // While in this loop, all path items should be directories
    while (next_slash != NULL) {
        // Evaluate the current "token"
        int token_length = next_slash - previous_slash - 1;
        if (token_length == 0) {
            wrap.errcode = -EINVAL;
            return wrap;
        }

        // call e2_find_dir_entry
        current_dir_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, token_length);
        // Check that the entry exists
        if (current_dir_entry == NULL) {
            wrap.errcode = -ENOENT;
            return wrap;
        }

        current_dir_inode = resolve_inode_number(current_dir_entry->inode);
        
        
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
            wrap.errcode = -ENOENT;
            return wrap;
        }

        // Continue the loop
        previous_slash = next_slash;
        next_slash = strchr(previous_slash + 1, '/');

        // Break if the string ends with a slash
        if (*(next_slash + 1) == '\0') break;
    }
    // Paths could end like /1/2/3 or /1/2/3/. 

    int len_last_string = strlen(previous_slash + 1);
    // If we have a path like /1/2/3, 3 will go here.
    if (next_slash == NULL) {
        // Try to return the item if found.
        struct ext2_dir_entry* found_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, len_last_string);

        if (found_entry != NULL) {
            wrap.entry = found_entry;
            wrap.errcode = 0;
            wrap.last_token = previous_slash + 1;
            return wrap;
        }
        else {
            wrap.entry = current_dir_entry;
            wrap.errcode = 1;
            wrap.last_token = previous_slash + 1;
            return wrap;
        }
    } 
    // If we have a path like /1/2/3/, return the directory if it is one
    else {
        // Find the last item and check that it's a directory
        struct ext2_dir_entry* found_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, len_last_string - 1);

        if (found_entry != NULL && found_entry->file_type == 2) {
            wrap.entry = found_entry;
            wrap.errcode = 0;
            wrap.last_token = previous_slash + 1;
            return wrap;
        } else {
            // This depends on the function but it's safer to return success and let them deal with it
            wrap.entry = found_entry;
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
 */
struct ext2_dir_entry* e2_create_file_setup(struct ext2_dir_entry* parent, char* name, int blocks_needed) {
    // Find/allocate a new inode for the file
    int inodeno = ex2_search_free_inode_bitmap();
    if (inodeno == -1) return NULL;
    
    struct ext2_inode* new_inode = resolve_inode_number(inodeno);
    struct ext2_inode* parent_inode = resolve_inode_number(parent->inode);

    // Initialize the inode (partially)

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
        return NULL;
    }


    // Check that there are enough free blocks in the system
    int free_blocks = ex2_find_free_blocks_count();

    // If there is no space left, undo the new inode mapping and dir entry and return
    if (free_blocks < blocks_needed) {
        ex2_unmark_inode_bitmap(inodeno);
        ex2_free_dir_entry(new_dir_entry);
        return NULL;
    }

    // superblock/group descriptors have been updated in helpers, so no work necessary here
    return new_dir_entry;

}

