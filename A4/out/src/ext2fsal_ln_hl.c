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

#include <stdbool.h>
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

extern fair_mutex inode_locks[32];
extern fair_mutex sb_lock;
extern fair_mutex gd_lock;

static void lock_locks(int src_lock_dir, int src_lock_file, int dst_lock_dir) {
    if (src_lock_dir < dst_lock_dir) {
        lock_lock(&inode_locks[src_lock_dir]);
        lock_lock(&inode_locks[src_lock_file]);
        lock_lock(&inode_locks[dst_lock_dir]);
    } else if (src_lock_dir > dst_lock_dir) {
        lock_lock(&inode_locks[dst_lock_dir]);
        lock_lock(&inode_locks[src_lock_dir]);
        lock_lock(&inode_locks[src_lock_file]);
    } else {
        // src_lock_dir == dst_lock_dir
        lock_lock(&inode_locks[src_lock_dir]);
        lock_lock(&inode_locks[src_lock_file]);
    }
}

static void unlock_locks(int src_dir, int src_file, int dst_dir) {
    unlock_lock(&inode_locks[src_dir]);
    unlock_lock(&inode_locks[src_file]);
    if (src_dir != dst_dir)
    unlock_lock(&inode_locks[dst_dir]);
}

int32_t ext2_fsal_ln_hl(const char *src,
                        const char *dst)
{
    /**
     * TODO: implement the ext2_ln_hl command here ...
     * src and dst are the ln command arguments described in the handout.
     */

    // Take the source, path walk through it.
    struct ex2_dir_wrapper src_path_return = e2_path_walk_absolute(src);
    // store locks
    int src_lock_file;
    int src_lock_dir = src_path_return.parent_inode;


    // what you need to do is get the inode of the source.
    // Check if the source has errcode 0 (source file exists), if not return ENOENT
    // Also, check if it's a directory
    if (src_path_return.errcode != 0 && src_path_return.errcode != 1) {
        return ENOENT;
    }
    if (src_path_return.entry == NULL) src_lock_file = 1;
    else src_lock_file = src_path_return.entry->inode - 1;
    if (src_path_return.errcode == 0 && src_path_return.entry->file_type == EXT2_FT_DIR) {
        unlock_lock(&inode_locks[src_lock_file]);
        unlock_lock(&inode_locks[src_lock_dir]);
        return EISDIR;
    }
    else if (src_path_return.errcode == 1) {
        unlock_lock(&inode_locks[src_lock_file]);
        unlock_lock(&inode_locks[src_lock_dir]);
        return ENOENT;
    }
    // Temporarily unlock the file and parent directory
    unlock_lock(&inode_locks[src_lock_file]);
    unlock_lock(&inode_locks[src_lock_dir]);


    // After, path walk to the destination
    struct ex2_dir_wrapper dst_path_return = e2_path_walk_absolute(dst);
    int dst_lock_dir;

    // Check if the dest has errcode 1 (everything until final item exists), if not return EEXIST
    if (dst_path_return.errcode == 0) {
        unlock_lock(&inode_locks[dst_path_return.entry->inode - 1]);
        unlock_lock(&inode_locks[dst_path_return.parent_inode]);
        if (dst_path_return.entry->file_type == EXT2_FT_DIR) {
            return EISDIR;
        }
        return EEXIST;
    }
    else if (dst_path_return.errcode != 1) {
        return ENOENT; 
    }

    dst_lock_dir = (dst_path_return.entry != NULL) ? dst_path_return.entry->inode - 1 : 1;

    // Unlock the parent directory entry
    unlock_lock(&inode_locks[dst_lock_dir]);


    // Lock the locks in order
    lock_locks(src_lock_dir, src_lock_file, dst_lock_dir);


    // Finally, regrab the three locks (src file, src parent, dst parent) with smallest first
    struct ext2_inode* source_inode = resolve_inode_number(src_lock_file);
    struct ext2_inode* source_dir = resolve_inode_number(src_lock_dir);
    struct ext2_inode* dest_dir = resolve_inode_number(dst_lock_dir);

    char* src_name = calloc(strlen(src_path_return.last_token) + 1, sizeof(char));
    strcpy(src_name, src_path_return.last_token);
    char* trailing_slash = strchr(src_name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }

    // temporarily allocate a name on the heap
    char* dst_name = calloc(strlen(dst_path_return.last_token) + 1, sizeof(char));
    strcpy(dst_name, dst_path_return.last_token);
    trailing_slash = strchr(dst_name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }

    // Finally, since we freed our locks previously,
    // we need to check that our files remain in a good state
    // e2_find_dir_entry(source_dir)
    struct ext2_dir_entry* new_src_entry = e2_find_dir_entry(source_dir, src_name, strlen(src_name), NULL);
    struct ext2_dir_entry* new_dst_entry = e2_find_dir_entry(dest_dir, dst_name, strlen(dst_name), NULL);
    free(src_name);
    if (new_src_entry == NULL) {
        unlock_locks(src_lock_dir, src_lock_file, dst_lock_dir);
        free(dst_name);
        return ENOENT;
    } else if (new_dst_entry != NULL) {
        unlock_locks(src_lock_dir, src_lock_file, dst_lock_dir);
        free(dst_name);
        return EEXIST;
    }
    // Update the src inode since it could change
    src_lock_file = new_src_entry->inode - 1;
    source_inode = resolve_inode_number(src_lock_file);


    // Finally, add a new directory entry to the parent directory pointing to the inode, with the name.
    struct ext2_inode* folder = resolve_inode_number(dst_lock_dir);

    lock_lock(&sb_lock);
    lock_lock(&gd_lock);

    struct ext2_dir_entry* hl = ex2_search_free_dir_entry(folder, dst_name, src_lock_file, NULL);
    unlock_lock(&gd_lock);
    unlock_lock(&sb_lock);
    free(dst_name);

    if (hl == NULL) {
        unlock_locks(src_lock_dir, src_lock_file, dst_lock_dir);
        return ENOSPC;
    }

    // whoops, gotta actually fill the info out!!!
    hl->file_type = src_path_return.entry->file_type;
    // also, resolve the inode and increment the link count.
    source_inode->i_links_count++;

    unlock_locks(src_lock_dir, src_lock_file, dst_lock_dir);

    return 0;
}
