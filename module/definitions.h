#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>

#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <linux/kernel.h>

#include <linux/workqueue.h>
// todo: some may be unused now

//// types

#define uint8 unsigned char
#define uint unsigned int
#define ulong unsigned long

//// macros

#define printInfo(...)  printk(KERN_INFO "stg_blkdev: " __VA_ARGS__)
#define printError(...) printk(KERN_ERR  "stg_blkdev: " __VA_ARGS__)

//// iterate directory

typedef int (*readdir_t)(void *, const char *, int, loff_t, u64, uint);

struct callback_context {
    struct dir_context ctx;
    readdir_t filler;
    void* context;
};

//// block device

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

#define BLK_DEV_NAME "sbd"

struct SteganographyBlockDevice {
    int devMajor;
    sector_t capacity;
    struct blk_mq_tag_set tag_set;
    struct request_queue *queue;
    struct gendisk *gdisk;
    struct BmpStorage *bmpS;
};

//// bmp

#define COLORS_PER_PIXEL 4
#define BMP_HEADER_SIZE 54

struct Bmp {
    struct file *fd;
    ulong size;
    char* path;

    uint width;
    uint height;

    uint headerSize;
    uint rowSize;
    uint8 padding;

    ulong virtualSize;
    ulong virtualOffset;

    void* filesim;

    struct Bmp* pnext;
};

struct BmpStorage {
    struct Bmp *bmps;
    ulong totalVirtualSize;
};
