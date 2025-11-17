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

#ifndef CSC369_E2FS_H
#define CSC369_E2FS_H

#include "ext2.h"
#include <string.h>

/**
 * TODO: add in here prototypes for any helpers you might need.
 * Implement the helpers in e2fs.c
 */

// .....
struct ex2_dir_wrapper* e2_path_walk_absolute(char* path);
struct ext2_dir_entry* e2_create_file_setup(struct ext2_dir_entry* parent, char* name);
struct ext2_inode* resolve_inode_number(unsigned int inodeno);
struct int ex2_search_free_block_bitmap();


// Structs for helper functions
struct ex2_dir_wrapper {
    // Null on error
    struct ext2_dir_entry* entry;
    // Negative if error
    // 0 if success
    // 1 if last item doesn't exist but items before do (entry thus has the parent inode)
    int errcode;
};

#endif