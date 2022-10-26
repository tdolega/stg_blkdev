#include <linux/fs.h>
#include <linux/blk-mq.h>
// #include <linux/vmalloc.h>

//// types

#define uint8 unsigned char
#define uint unsigned int
#define ulong unsigned long

//// macros

#define printInfo(...)  printk(KERN_INFO "stg_blkdev: " __VA_ARGS__)
#define printError(...) printk(KERN_ERR  "stg_blkdev: " __VA_ARGS__)

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

struct SbdWorker {
    struct work_struct work;
    struct request *rq;
};

//// bmp

#define COLORS_PER_PIXEL 4
#define USED_BITS_PER_PIXEL 2
#define BMP_HEADER_SIZE 54

struct Bmp {
    struct file *fd;
    ulong size;

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
    char* backingPath;
};
