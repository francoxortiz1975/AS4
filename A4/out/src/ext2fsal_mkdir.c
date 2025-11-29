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

#include "ext2fsal.h"
#include "e2fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>


int32_t ext2_fsal_mkdir(const char *path)
{
    /**
     * TODO: implement the ext2_mkdir command here ...
     * the argument path is the path to the directory that is to be created.
     */

     /* This is just to avoid compilation warnings, remove this line when you're done. */
    // Use a reader lock while path-walking
    // TODO

    // Take the path, path walk through it.
    struct ex2_dir_wrapper path_return = e2_path_walk_absolute(path);
    unsigned int inodenum;
    // Make sure to free the dir_wrapper after use!
    if (path_return.errcode != 0 && path_return.errcode != 1) {
        return EEXIST;
    }
    else if (path_return.errcode == 0) { // This means the entry is an existing inode
        // If it's a regular file, return ENOENT
        
        // Also unlock the parent directory
        pthread_mutex_unlock(&inode_locks[path_return.parent_inode]);

        if (path_return.entry->file_type == EXT2_FT_REG_FILE) {
            // Unlock the inode
            pthread_mutex_unlock(&inode_locks[path_return.entry->inode - 1]);
            return ENOENT;
        } else if (path_return.entry->file_type == EXT2_FT_DIR) {
            pthread_mutex_unlock(&inode_locks[path_return.entry->inode - 1]);
            return EEXIST;
        } else {
            // TODO check what happens if it's a symlink
            pthread_mutex_unlock(&inode_locks[path_return.entry->inode - 1]);
            return EEXIST;
        }

    } 
    struct ext2_inode* parentinode;
    if (path_return.entry == NULL) {
        parentinode = root_inode;
        inodenum = 1;
    } else {
        inodenum = path_return.entry->inode - 1;
        // The entry is the parent inode
        parentinode = resolve_inode_number(inodenum);

    }


    // get the name of the new inode, removing trailing slashes
    char* name = calloc(strlen(path_return.last_token) + 1, sizeof(char));
    strcpy(name, path_return.last_token);
    char* trailing_slash = strchr(name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }

    // Before creating an inode and file for the directory, check if there are 

    // Create a file with the helper function
    // Note this function claims the sb and gd locks
    struct ext2_dir_entry* newfile = e2_create_file_setup(path_return.entry, name, 1);
    free(name);
    if (newfile == NULL) {
        pthread_mutex_unlock(&inode_locks[path_return.entry->inode - 1]);
        pthread_mutex_unlock(&gd_lock);
        pthread_mutex_unlock(&sb_lock);
        return ENOSPC;
    }
    newfile->file_type = EXT2_FT_DIR;

    struct ext2_inode* newinode = resolve_inode_number(newfile->inode - 1);
    
    // Set the file type of the inode
    newinode->i_mode &= ~0xF000;
    newinode->i_mode |= EXT2_S_IFDIR;
    // Set links count to 2
    newinode->i_links_count = 2;

    // Now, allocate a data block for the file
    // This also cannot be null since create_file_setup would return an error if there is no space
    int blockno = ex2_search_free_block_bitmap();

    gd->bg_used_dirs_count++;

    pthread_mutex_unlock(&gd_lock);
    pthread_mutex_unlock(&sb_lock);

    newinode->i_block[0] = blockno;
    newinode->i_blocks = 2;

    // Since the logic for handling free block count is in e2_create_file_setup,
    // No checking for free space is needed.
    // (because these two entries will not take up 1024 bytes)
    // Write . and .. into it.
    struct ext2_dir_entry* dot = ex2_search_free_dir_entry(newinode, ".", newfile->inode - 1, NULL);
    struct ext2_dir_entry* dotdot = ex2_search_free_dir_entry(newinode, "..", inodenum, NULL);

    dot->file_type = EXT2_FT_DIR;
    dotdot->file_type = EXT2_FT_DIR;

    parentinode->i_links_count++;

    // increment used_dirs_count in the superblock

    // free the new file lock
    pthread_mutex_unlock(&inode_locks[newfile->inode - 1]);
    
    // free the parent directory lock
    pthread_mutex_unlock(&inode_locks[inodenum]);



    return 0;
}