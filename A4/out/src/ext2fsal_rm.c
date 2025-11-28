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
    
    //get inode (inode numbers in entries are 1-based, resolve expects 0-based)
    struct ext2_inode *file_inode = resolve_inode_number(result.entry->inode - 1);
    //decreaser file link count
    file_inode->i_links_count--;
    
    // if no more links, clean.
    if (file_inode->i_links_count == 0) {
        // Set deletion time
        file_inode->i_dtime = 1;
        
        //free data blocks
        int blocks_used = (file_inode->i_size + 1023) / 1024;
        free_blocks(file_inode, 0, blocks_used);  // free all blocks
        
        //free inode (pass 0-based index)
        ex2_unmark_inode_bitmap(result.entry->inode - 1);
    }
    
    // Extract filename from path (everything after the last /)
    const char *filename = strrchr(path, '/');
    if (filename == NULL) {
        filename = path;  // No slash, entire path is filename
    } else {
        filename++;  // Skip the slash
    }
    
    // Get parent directory inode
    struct ext2_inode *parent_inode = NULL;
    
    // Check if file is directly in root (path like /test.txt)
    const char *second_slash = strchr(path + 1, '/');
    if (second_slash == NULL) {
        // File is in root directory
        parent_inode = root_inode;
    } else {
        // File is in a subdirectory, need to walk to parent
        char parent_path[strlen(path) + 1];
        strcpy(parent_path, path);
        char *last_slash = strrchr(parent_path, '/');
        *last_slash = '\0';
        
        struct ex2_dir_wrapper parent_result = e2_path_walk_absolute(parent_path);
        
        if (parent_result.errcode == 0 && parent_result.entry->file_type == EXT2_FT_DIR) {
            parent_inode = resolve_inode_number(parent_result.entry->inode - 1);
        }
    }
    
    if (parent_inode != NULL) {
        remove_dir_entry_from_parent(parent_inode, filename);
    }

    return 0;
}