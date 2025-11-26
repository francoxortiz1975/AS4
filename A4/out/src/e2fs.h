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
#include <pthread.h>
#include <stdio.h>

/**
 * TODO: add in here prototypes for any helpers you might need.
 * Implement the helpers in e2fs.c
 */

// .....
struct ex2_dir_wrapper e2_path_walk_absolute(const char* path);
struct ext2_dir_entry* e2_create_file_setup(struct ext2_dir_entry* parent, char* name, int blocks_needed);
struct ext2_inode* resolve_inode_number(unsigned int inodeno);
struct ext2_dir_entry* ex2_search_free_dir_entry(struct ext2_inode* folder, char* name, unsigned int inode);
void ex2_free_dir_entry(struct ext2_dir_entry* entry);
int ex2_search_free_block_bitmap();

void write_block_data(int block_num, char *data);
int file_init(struct ext2_dir_entry *file_entry, const char *source_path);
void free_blocks(struct ext2_inode *inode, int new_blocks, int old_blocks);
void assign_blocks(struct ext2_inode *inode, int old_blocks, int new_blocks_needed);
void copy_to_file(struct ext2_inode *inode, FILE *source, int blocks_needed);
int file_overwrite(struct ext2_dir_entry *existing_entry, const char *source_path);
int file_exists(const char *filepath);
int create_new_file(struct ext2_dir_entry *parent_entry, const char *filename, const char *src);
int copy_into_directory(struct ext2_dir_entry *dir_entry, const char *src);



// Structs for helper functions
struct ex2_dir_wrapper {
    // Null on error
    struct ext2_dir_entry* entry;
    // The parent directory inode. 0-indexed. If the entry is '/', return -1;
    int parent_inode;
    // Negative if error
    // 0 if success
    // 1 if last item doesn't exist but items before do (entry thus has the parent inode)
    int errcode;
    // Pointer to the last token. Not allocated with malloc, uses the input path
    // May have a trailing slash
    char* last_token;
};

#endif