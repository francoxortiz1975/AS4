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

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

// Global variables
extern unsigned char* disk; 
extern struct ext2_super_block *sb;
extern struct ext2_group_desc* gd;
extern unsigned char* inode_table;
extern struct ext2_inode* root_inode;
extern pthread_rwlock_t inode_locks[32];
extern pthread_rwlock_t sb_lock;
extern pthread_rwlock_t gd_lock;


// Initializes the ext2 file system
// Called only once during the server initialization
//
// image is a pointer to a zero terminated string that is a full path to valid ext2 disk image
void ext2_fsal_init(const char *image);

// Destroys the ext2 file system
void ext2_fsal_destroy();

// src is a pointer to a zero terminated string
// dst is a pointer to a zero terminated string
//
// returns 0 if the operation completed succefully. 
// Otherwise, an error may be returned (see handout).
int32_t ext2_fsal_cp(const char *src,
                     const char *dst);

// src is a pointer to a zero terminated string
// dst is a pointer to a zero terminated string
//
// returns 0 if the operation completed succefully. 
// Otherwise, an error may be returned (see handout).
int32_t ext2_fsal_ln_hl(const char *src,
                        const char *dst);

// src is a pointer to a zero terminated string
// dst is a pointer to a zero terminated string
//
// returns 0 if the operation completed succefully. 
// Otherwise, an error may be returned (see handout).
int32_t ext2_fsal_ln_sl(const char *src,
                        const char *dst);

// path is a pointer to a zero terminated string
//
// returns 0 if the operation completed succefully. 
// Otherwise, an error may be returned (see handout).
int32_t ext2_fsal_rm(const char *path);

// path is a pointer to a zero terminated string
//
// returns 0 if the operation completed succefully. 
// Otherwise, an error may be returned (see handout).
int32_t ext2_fsal_mkdir(const char *path);
