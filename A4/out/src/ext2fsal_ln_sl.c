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


int32_t ext2_fsal_ln_sl(const char *src,
                        const char *dst)
{
    /**
     * TODO: implement the ext2_ln_sl command here ...
     * src and dst are the ln command arguments described in the handout.
     */

    // get the string length and divide it by 1024 to get the number of blocks
    int srclen = strlen(src);
    int iterations = srclen >> 10;
    if (srclen % 1024 != 0) iterations++;

    // We don't have to validate the source is correct
    // just validate the destination
    struct ex2_dir_wrapper dst_path_return = e2_path_walk_absolute(dst);

    // check if the destination has errcode
    if (dst_path_return.errcode != 1) {
        if (dst_path_return.errcode == 0) {
            // unlock the file and parent directory
            unlock_lock(&inode_locks[dst_path_return.entry->inode - 1]);
            unlock_lock(&inode_locks[dst_path_return.parent_inode]);

            if (dst_path_return.entry->file_type == EXT2_FT_DIR) {
                return EISDIR;
            }
            else {
                return EEXIST;
            }
        }
        return ENOENT;         
    }


    char* name = calloc(strlen(dst_path_return.last_token) + 1, sizeof(char));
    strcpy(name, dst_path_return.last_token);
    char* trailing_slash = strchr(name, '/');
    if (trailing_slash != NULL) {
        *trailing_slash = '\0';
    }
    // Add a new file.
    // Note this claims the sb and gd locks
    struct ext2_dir_entry* newfile = e2_create_file_setup(dst_path_return.entry, name, iterations);
    free(name);
    if (newfile == NULL) {
        unlock_lock(&inode_locks[dst_path_return.entry->inode - 1]);
        unlock_lock(&gd_lock);
        unlock_lock(&sb_lock);
        return ENOSPC;
    }
    newfile->file_type = EXT2_FT_SYMLINK;
    // now, get the inode from it
    struct ext2_inode* newinode = resolve_inode_number(newfile->inode - 1);
    newinode->i_mode &= ~0xF000;
    newinode->i_mode |= EXT2_S_IFLNK;
    // set file size
    newinode->i_size = srclen;
    newinode->i_blocks = iterations * 2;

    for (int i = 0; i < iterations; i++) {
        // allocate a new data block
        // We can assume this will succeed because we checked for n free spots in e2_create_file_setup
        int blockno = ex2_search_free_block_bitmap();
        // resolve it and write the data into it
        // but also, truncate the file
        char* block = (char*)(disk + (1024 * blockno));
        // truncate the block before writing
        memset(block, 0, 1024);
    
        // write your data.
        strncpy(block, src, 1024);
        newinode->i_block[i] = blockno;
    }

    // Free the parent directory lock
    unlock_lock(&inode_locks[dst_path_return.parent_inode]);
    // Free the symlink
    unlock_lock(&inode_locks[newfile->inode - 1]);
    // Free sb and gd locks
    unlock_lock(&gd_lock);
    unlock_lock(&sb_lock);

    return 0;
}
