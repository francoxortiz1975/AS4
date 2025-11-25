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


int32_t ext2_fsal_rm(const char *path){
    /**
     * TODO: implement the ext2_rm command here ...
     * the argument 'path' is the path to the file to be removed.
     */

    //path walk
    struct ex2_dir_wrapper result = e2_path_walk_absolute(path);
    
    //if not exits, error
    if (result.errcode != 0) {
        return -ENOENT;
    }
    
    //if not directory, error
    if (result.entry->file_type == EXT2_FT_DIR) {
        return -EISDIR;
    }
    
    //get inode
    struct ext2_inode *file_inode = resolve_inode_number(result.entry->inode);
    //decreaser file link count
    file_inode->i_links_count--;
    
    // if no more links, clean.
    if (file_inode->i_links_count == 0) {
        //free data blocks
        int blocks_used = (file_inode->i_size + 1023) / 1024;
        free_blocks(file_inode, 0, blocks_used);  // free all blocks
        
        //free inode
        ex2_unmark_inode_bitmap(result.entry->inode);
    }
    
    // mark as free directory
    ex2_free_dir_entry(result.entry);

    return 0;
}