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
    // Make sure to free the dir_wrapper after use!
    if (path_return.errcode < 0) {
        return -EEXIST;
    }
    else if (path_return.errcode == 0) { // This means the entry is an existing inode
        // If it's a regular file, return ENOENT
        if (path_return.entry->file_type == EXT2_FT_REG_FILE) {
            return -ENOENT;
        } else if (path_return.entry->file_type == EXT2_FT_DIR) {
            return -EEXIST;
        } else {
            // TODO check what happens if it's a symlink
            fprintf(stderr, "ext2fsal_mkdir: Unknown what occurs if ending path element is a symlink\n");
            exit(1);
        }

    } 
    // The entry is the parent inode
    struct ext2_inode* parentinode = resolve_inode_number(path_return.entry->inode);


    // get the name of the new inode, removing trailing slashes
    char* name = calloc(strlen(path_return.last_token) + 1, sizeof(char));
    strcpy(name, path_return.last_token);
    char* trailing_slash = strchr(name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }

    // Before creating an inode and file for the directory, check if there are 

    // Create a file with the helper function
    struct ext2_dir_entry* newfile = e2_create_file_setup(path_return.entry, name, 1);
    if (newfile == NULL) {
        
        return -ENOSPC;
    }

    struct ext2_inode* newinode = resolve_inode_number(newfile->inode);
    
    // Set the file type of the inode
    newinode->i_mode &= 0x0FFF;
    newinode->i_mode |= EXT2_S_IFDIR;
    // Set links count to 2
    newinode->i_links_count = 2;

    // Now, allocate a data block for the file
    int blockno = ex2_search_free_block_bitmap();

    newinode->i_block[0] = blockno;

    // Since the logic for handling free block count is in e2_create_file_setup,
    // No checking for free space is needed.
    // (because these two entries will not take up 1024 bytes)
    // Write . and .. into it.
    struct ext2_dir_entry* dot = ex2_search_free_dir_entry(newinode, ".", newfile->inode);
    struct ext2_dir_entry* dotdot = ex2_search_free_dir_entry(newinode, "..", path_return.entry->inode);
    
    dot->file_type = EXT2_FT_DIR;
    dotdot->file_type = EXT2_FT_DIR;

    parentinode->i_links_count++;



    
    return 0;
}