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

extern fair_mutex inode_locks[32];
extern fair_mutex sb_lock;
extern fair_mutex gd_lock;


int32_t ext2_fsal_cp(const char *src,
                     const char *dst)
{
    /**
     * TODO: implement the ext2_cp command here ...
     * Arguments src and dst are the cp command arguments described in the handout.
     */

     /* This is just to avoid compilation warnings, remove these 2 lines when you're done. */
        //CHECK src if valid file
    if (!file_exists(src)) return ENOENT;
    
    //CHECK path with pathwalk helper
    struct ex2_dir_wrapper dst_result = e2_path_walk_absolute(dst);
    
    int parent_lock_num = dst_result.parent_inode;
    
    int returncode;
    //IF 0, MEANS final entry exists
    if (dst_result.errcode == 0) {
        int file_lock_num = dst_result.entry->inode - 1;
        //IF this entry is a FOLDER
        if (dst_result.entry->file_type == EXT2_FT_DIR) {
            //COPY HERE
            returncode = copy_into_directory(dst_result.entry, src);
            unlock_lock(&inode_locks[parent_lock_num]);
            unlock_lock(&inode_locks[file_lock_num]);
            return returncode;
        } else {
            // entry is FILE
            returncode = file_overwrite(dst_result.entry, src);
            unlock_lock(&inode_locks[parent_lock_num]);
            unlock_lock(&inode_locks[file_lock_num]);
            return returncode;
        }
    }
    else if (dst_result.errcode == 1) {
        // if 1, father exists, 
        returncode = create_new_file(dst_result.entry, dst_result.last_token, src);    
        unlock_lock(&inode_locks[parent_lock_num]);
        return returncode;
    }
    else {
        return ENOENT;
    }

    return 0;
}