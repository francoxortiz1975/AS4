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


int32_t ext2_fsal_ln_hl(const char *src,
                        const char *dst)
{
    /**
     * TODO: implement the ext2_ln_hl command here ...
     * src and dst are the ln command arguments described in the handout.
     */

     /* This is just to avoid compilation warnings, remove these 3 lines when you're done. */
    // Take the source, path walk through it.
    struct ex2_dir_wrapper src_path_return = e2_path_walk_absolute(src);


    // what you need to do is get the inode of the source.
    // Check if the source has errcode 0 (source file exists), if not return ENOENT
    // Also, check if it's a directory
    if (src_path_return.errcode != 0) {
        return ENOENT;
    }
    else if (src_path_return.errcode == 0 && src_path_return.entry->file_type == EXT2_FT_DIR) {
        return EISDIR;
    }

    unsigned int source_inode_num = src_path_return.entry->inode - 1;


    // After, path walk to the destination
    struct ex2_dir_wrapper dst_path_return = e2_path_walk_absolute(dst);
    
    // Check if the dest has errcode 1 (everything until final item exits), if not return EEXIST
    if (dst_path_return.errcode == 0 && dst_path_return.entry->file_type == EXT2_FT_DIR) {
        return EISDIR;
    }
    else if (dst_path_return.errcode < 0) {
        return ENOENT; 
    }
    else if (dst_path_return.errcode == 0) {
        return EEXIST;
    }
    // Finally, add a new directory entry to the parent directory pointing to the inode, with the name.
    int dest_parent_inode = (dst_path_return.entry != NULL) ? dst_path_return.entry->inode - 1 : 1;
    struct ext2_inode* folder = resolve_inode_number(dest_parent_inode);
    // temporarily allocate a name on the heap
    char* name = calloc(strlen(dst_path_return.last_token) + 1, sizeof(char));
    strcpy(name, dst_path_return.last_token);
    char* trailing_slash = strchr(name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }


    struct ext2_dir_entry* hl = ex2_search_free_dir_entry(folder, name, source_inode_num);
    free(name);
    if (hl == NULL) {
        return ENOSPC;
    }
    // whoops, gotta actually fill the info out!!!
    hl->file_type = src_path_return.entry->file_type;
    // also, resolve the inode and increment the link count.
    struct ext2_inode* source_inode = resolve_inode_number(source_inode_num);
    source_inode->i_links_count++;

    return 0;
}
