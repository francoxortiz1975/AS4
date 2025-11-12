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


// Define essential/commonly used global variables
// The disk itself, via mmap
unsigned char *disk;
// The superblock and group descriptor table
struct ext2_super_block *sb;
struct ext2_group_desc* gd;
// The inode table
char* inode_table;
// The root directory inode, since it's used on every path walk
struct ext2_inode* root_inode;


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

}

void ext2_fsal_destroy()
{
    /**
     * TODO: Cleanup tasks, e.g., destroy synchronization primitives, munmap the image, etc.
     */
    // TODO free the mmap of disk with munmap() (probably)
}