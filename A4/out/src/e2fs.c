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
extern char* inode_table;
extern struct ext2_inode* root_inode;


/**
 * This function is a shortcut for functions that return ext2_dir_wrapper pointers.
 * It is used when there is an error.
 */
static struct ex2_dir_wrapper* e2_return_error_code(int error) {
    struct ex2_dir_wrapper* errorval = calloc(1, sizeof(struct ext2_dir_wrapper));
    if (errorval == NULL) {
        // Out of memory
        exit(1);
    }
    errorval->errcode = error;
    return errorval;
} 
/**
 * This function is a shortcut for functions that return ext2_dir_wrapper pointers.
 * It is used when there is a success
 */
static struct ex2_dir_wrapper* e2_return_success(struct ext2_dir_entry* entry, int code) {
    struct ex2_dir_wrapper* successval = calloc(1, sizeof(struct ext2_dir_wrapper));
    if (successval == NULL) {
        // Out of memory
        exit(1);
    }
    successval->entry = entry;
    successval->errcode = code;
    return successval;
}

/**
 * This function searches the inode bitmap and returns the index of the first free non-reserved entry.
 */
static int ex2_search_inode_bitmap() {
    // get the inode bitmap from the group descriptor
    char* inode_bitmap = (disk + (1024 * gd->bg_inode_bitmap));
    
}

/**
 * This function gets an inode number from the directory entry number and resolves
 * it into a pointer to an ext2_inode.
 */
struct ext2_inode* resolve_inode_number(unsigned int inodeno) {
    return (struct ext2_inode*)(inode_table + sizeof(struct ext2_inode) * inodeno);
}

/**
 * This function attempts to find a directory entry given a current directory inode
 * and a name. If successful, it will return the entry. If not, it will return NULL.
 */
struct ext2_dir_entry* e2_find_dir_entry(struct ext2_inode* directory, char* name, int name_length) {
    if (directory->i_mode >> 12 != EXT2_S_IFDIR >> 12) {
        fprintf("e2_find_dir_entry: This was not called with a directory inode!\n");
        exit(1);
    }
    // From the inode, get the direct blocks
    // We can assume that directories only have direct blocks
    for (int i = 0; i < 12; i++) {
        int block_num = directory-i_block[i];
        // Since blocks gives us double the actual count
        if (i + 1 > directory->i_blocks / 2) break;

        // Iterate through the linked list of entries
        struct ext2_dir_entry* entry = (struct ext2_dir_entry*)(disk + (1024 * block_num));
        while (1) {

            // If the name matches
            if (
                name_length == entry->name_len &&
                strncmp(name, entry->name, name_len) == 0
            ) {
                return entry;
            }

            // Go to the next entry
            entry = (struct ext2_dir_entry *)((void*)entry + entry->rec_len);
            // Break if at the end
            if (((void*)entry - (void*)disk) % 1024 == 0) {
                break;
            }
        }
    }
    return NULL;
}

/**
 * This function implements a path-walk, given an absolute path string.
 * It returns an ex2_dir_wrapper pointer, a struct containing:
 * - a pointer to a directory entry (file, symlink, or directory) which is NULL on failure
 *   It can also be NULL if errcode returns 1 and the path is from the root dir, like with "/1"
 * - an error code which is 0 on success, 1 if the last item does not exist, and an errno value on error. 
 *   If errcode is 0, the pointer points to the file entry itself. If errcode is 1, it points to the parent directory entry 
 *   (which can be NULL if parent is root)
 * It is allocated via the heap, so it must be free'd after it is done being used!
 * Possible errors:
 * - EFAULT if the path is not absolute
 * - EINVAL if there is a path with 0 length item like "/1//3"
 * - ENOENT if there is an item that doesn't exist
 */
struct ex2_dir_wrapper* e2_path_walk_absolute(char* path) {
    // Check that the first character of the string is / (so it's properly absolute)
    if (path[0] != '/') return e2_return_error_code(-EFAULT);

    // Since it does start with /, start with inode 2 (the / directory)

    int len_string = strlen(path);


    // Start at the first directory number
    char* previous_slash = path;
    char* next_slash = strchr(previous_slash + 1, '/');
    struct ext2_inode* current_dir_inode = root_inode;
    struct ext2_dir_entry* current_dir_entry = NULL;

    // While in this loop, all path items should be directories
    while (next_slash != NULL) {
        // Evaluate the current "token"
        int token_length = next_slash - previous_slash - 1;
        if (token_length == 0) return e2_return_error_code(-EINVAL);

        // call e2_find_dir_entry
        current_dir_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, token_length);
        // Check that the entry exists
        if (current_dir_entry == NULL) return e2_return_error_code(-ENOENT);

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

        // TODO: evaluate symlink directories
        // Hm, wouldn't that require its own path traversal?

        // Check that it's the correct file type (directory)
        if (file_type != 'd') return e2_return_error_code(-ENOENT);

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

        if (found_entry != NULL)
            return e2_return_success(found_entry, 0);
        else
            return e2_return_success(current_dir_entry, 1);
    } 
    // If we have a path like /1/2/3/, return the directory if it is one
    else {
        // Find the last item and check that it's a directory
        struct ext2_dir_entry* found_entry = e2_find_dir_entry(current_dir_inode, previous_slash + 1, len_last_string - 1);

        if (found_entry != NULL && found_entry->file_type == 2) {
            return e2_return_success(found_entry, 0);
        } else {
            return e2_return_error_code(-ENOENT);
        }
    }
}

struct ext2_dir_entry* e2_create_file_setup(struct ext2_dir_entry* parent) {

}

