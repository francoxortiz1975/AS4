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
pthread_mutex_t inode_locks[32];
// An array of reference counts, used for multi-path operations (ln_hl)
//char reference_counts[32];
// Global locks for the superblock and group descriptor
// ALWAYS LOCK THE SB before the GD!
pthread_mutex_t sb_lock;
pthread_mutex_t gd_lock;

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
        pthread_mutex_t* lock = &inode_locks[i];
        if (pthread_mutex_init(lock, NULL) != 0) {
            // Check for errors, exit if encountered
            exit(1);
        } 
    }
    // Initialize reference_counts
    //memset(reference_counts, 0, 32);
    // Initialize superblock and group descriptor locks, exiting on errors
    if (pthread_mutex_init(&sb_lock, NULL) || pthread_mutex_init(&gd_lock, NULL)) {
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
        pthread_mutex_destroy(&inode_locks[i]);
    }

    // Free the superblock and group descriptor locks
    pthread_mutex_destroy(&sb_lock);
    pthread_mutex_destroy(&gd_lock);

    // Unmap the image
    munmap(disk, 128 * 1024);

    // Woohoo!
}