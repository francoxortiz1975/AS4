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


int32_t ext2_fsal_cp(const char *src,
                     const char *dst)
{
    /**
     * TODO: implement the ext2_cp command here ...
     * Arguments src and dst are the cp command arguments described in the handout.
     */

    printf("=== DEBUG: Starting ext2_fsal_cp ===\n");
    printf("DEBUG: src='%s'\n", src);
    printf("DEBUG: dst='%s'\n", dst);
    fflush(stdout);

    // CHECK src if valid file
    printf("DEBUG: Checking if source file exists...\n");
    fflush(stdout);
    
    if (!file_exists(src)) {
        printf("DEBUG: Source file does not exist, returning -ENOENT\n");
        fflush(stdout);
        return -ENOENT;
    }
    
    printf("DEBUG: Source file exists, continuing...\n");
    fflush(stdout);

    // CHECK path with pathwalk helper
    printf("DEBUG: Calling e2_path_walk_absolute with dst='%s'\n", dst);
    fflush(stdout);
    
    struct ex2_dir_wrapper dst_result = e2_path_walk_absolute(dst);
    
    printf("DEBUG: e2_path_walk_absolute returned errcode=%d\n", dst_result.errcode);
    fflush(stdout);

    // IF 0, MEANS final entry exists
    if (dst_result.errcode == 0) {
        printf("DEBUG: Destination exists (errcode=0)\n");
        printf("DEBUG: Checking dst_result.entry pointer: %p\n", (void*)dst_result.entry);
        fflush(stdout);
        
        if (dst_result.entry == NULL) {
            printf("DEBUG: ERROR - dst_result.entry is NULL!\n");
            fflush(stdout);
            return -ENOENT;
        }
        
        printf("DEBUG: dst_result.entry->file_type = %d\n", dst_result.entry->file_type);
        printf("DEBUG: EXT2_FT_DIR constant = %d\n", EXT2_FT_DIR);
        fflush(stdout);

        // IF this entry is a FOLDER
        if (dst_result.entry->file_type == EXT2_FT_DIR) {
            printf("DEBUG: Destination is a directory, calling copy_into_directory\n");
            fflush(stdout);
            return copy_into_directory(dst_result.entry, src);
        } else {
            printf("DEBUG: Destination is a file, calling file_overwrite\n");
            fflush(stdout);
            // entry is FILE
            return file_overwrite(dst_result.entry, src);
        }
    }
    else if (dst_result.errcode == 1) {
    printf("DEBUG: Parent directory exists, creating new file (errcode=1)\n");
    printf("DEBUG: dst_result.parent_inode pointer: %p\n", (void*)dst_result.parent_inode);
    printf("DEBUG: dst_result.last_token: '%s'\n", dst_result.last_token ? dst_result.last_token : "NULL");
    fflush(stdout);
    
    // Add safety check - now check parent_inode instead of entry
    if (dst_result.parent_inode == NULL) {
        printf("DEBUG: ERROR - Parent directory inode is NULL!\n");
        fflush(stdout);
        return -ENOENT;
    }
    
    printf("DEBUG: About to call create_new_file_in_inode...\n");
    fflush(stdout);
    
    // Need to call a version that takes inode directly, or modify create_new_file
    int result = create_new_file_in_inode(dst_result.parent_inode, dst_result.last_token, src);
    
    printf("DEBUG: create_new_file_in_inode returned: %d\n", result);
    fflush(stdout);
    
    return result;
    }
    else {
        printf("DEBUG: Path walk failed with errcode=%d, returning -ENOENT\n", dst_result.errcode);
        fflush(stdout);
        return -ENOENT;
    }

    // This should never be reached
    printf("DEBUG: ERROR - Reached end of function unexpectedly!\n");
    fflush(stdout);
    return 0;
}