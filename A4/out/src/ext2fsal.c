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
// i assume this is fine?
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <pthread.h>



// Define essential/commonly used global variables
// The disk itself, via mmap
unsigned char *disk;
// The superblock and group descriptor table
struct ext2_super_block *sb;
struct ext2_group_desc* gd;
// The inode table
unsigned char* inode_table;
// The root directory inode, since it's used on every path walk
struct ext2_inode* root_inode;
// An array of locks for each inode
fair_mutex inode_locks[32];
// An array of reference counts, used for multi-path operations (ln_hl)
//char reference_counts[32];
// Global locks for the superblock and group descriptor
// ALWAYS LOCK THE SB before the GD!
fair_mutex sb_lock;
fair_mutex gd_lock;

void ext2_fsal_init(const char* image)
{
    /**
     * TODO: Initialization tasks, e.g., initialize synchronization primitives used,
     * or any other structures that may need to be initialized in your implementation,
     * open the disk image by mmap-ing it, etc.
     */

    // Have to open the disk image by mmap-ing it
    int fd = open(image, O_RDWR);

    // Define the disk
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Declare superblock and group descriptor table
    sb = (struct ext2_super_block *)(disk + 1024);

    gd = (struct ext2_group_desc*)(disk + (1024*2));

    inode_table = (disk + (1024 * gd->bg_inode_table));

    root_inode = (struct ext2_inode*)(inode_table + sizeof(struct ext2_inode) * 1);

    // Initialize every lock
    for (int i = 0; i < 32; i++)
    {
        fair_mutex* lock = &inode_locks[i];
        if (init_lock(lock) != 0) {
            // Check for errors, exit if encountered
            exit(1);
        } 
    }
    // Initialize reference_counts
    //memset(reference_counts, 0, 32);
    // Initialize superblock and group descriptor locks, exiting on errors
    if (init_lock(&sb_lock) || init_lock(&gd_lock)) {
        exit(1);
    }
    

}

void ext2_fsal_destroy()
{
    /**
     * TODO: Cleanup tasks, e.g., destroy synchronization primitives, munmap the image, etc.
     */
    // Free each inode lock
    for (int i = 0; i < 32; i++) {
        destroy_lock(&inode_locks[i]);
    }

    // Free the superblock and group descriptor locks
    destroy_lock(&sb_lock);
    destroy_lock(&gd_lock);

    // Unmap the image
    munmap(disk, 128 * 1024);

    // Woohoo!
}