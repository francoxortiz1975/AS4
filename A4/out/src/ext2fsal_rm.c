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
#include <time.h>


int32_t ext2_fsal_rm(const char *path){
    /**
     * TODO: implement the ext2_rm command here ...
     * the argument 'path' is the path to the file to be removed.
     */
    
    //path walk
    struct ex2_dir_wrapper result = e2_path_walk_absolute(path);
    
    //if not exits, error
    if (result.errcode != 0) {
        // If the parent dir existed, unlock it
        if (result.errcode == 1) {
            unlock_lock(&inode_locks[result.parent_inode]);
        }
        return ENOENT;
    }
    // Note it now claims 2 locks
    int parent_lock_num = result.parent_inode;
    int file_lock_num = result.entry->inode - 1;


    // if directory, error
    if (result.entry->file_type == EXT2_FT_DIR) {
        unlock_lock(&inode_locks[parent_lock_num]);
        unlock_lock(&inode_locks[file_lock_num]);
        return EISDIR;
    }
    
    // get inode (inode numbers in entries are 1-based, resolve expects 0-based)
    struct ext2_inode *file_inode = resolve_inode_number(result.entry->inode - 1);
    // decrease file link count
    file_inode->i_links_count--;
    
    // if no more links, clean.
    if (file_inode->i_links_count == 0) {
        // Set deletion time
        file_inode->i_dtime = (unsigned int)time(NULL);
        
        //free data blocks
        int blocks_used = (file_inode->i_size + 1023) / 1024;
        free_blocks(file_inode, 0, blocks_used);  // free all blocks
        
        //free inode (pass 0-based index)
        lock_lock(&sb_lock);
        lock_lock(&gd_lock);
        ex2_unmark_inode_bitmap(result.entry->inode - 1);
        unlock_lock(&gd_lock);
        unlock_lock(&sb_lock);
    }

    // Remove the directory entry
    ex2_free_dir_entry(result.entry, result.prev_entry);
    unlock_lock(&inode_locks[parent_lock_num]);
    unlock_lock(&inode_locks[file_lock_num]);
    return 0;
}