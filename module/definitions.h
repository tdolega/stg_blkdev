#include <linux/fs.h>
#include <linux/blk-mq.h>

//// types

// TODO: fix all of those to proper u16, u32, etc.
#define uint unsigned int
#define uint8 unsigned char
#define uint16 unsigned short
#define ulong unsigned long

//// macros

#define printInfo(...)  printk(KERN_INFO  "stg_blkdev: " __VA_ARGS__)
#define printError(...) printk(KERN_ERR   "stg_blkdev: " __VA_ARGS__)
#define printDebug(...) printk(KERN_DEBUG "stg_blkdev: " __VA_ARGS__)

//// block device

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

#define CTL_DEV_NAME "stg_manager"
#define BLK_DEV_NAME "stg"

#define IOCTL_DEV_ADD 55001
#define IOCTL_DEV_REMOVE 55002
#define MAX_BACKING_LEN 1024

#define RW_BUF_SIZE PAGE_SIZE
#define RW_BUF_PIXELS (PAGE_SIZE / COLORS_PER_PIXEL)

struct SteganographyBlockDevice {
    int devMajor;
    char letter;
    sector_t capacity;
    struct blk_mq_tag_set tag_set;
    struct request_queue *queue;
    struct gendisk *gdisk;
    struct BmpStorage *bmpS;

    struct SteganographyBlockDevice *pnext;
};

struct SteganographyControlDevice {
    int devMajor;
    char letter;
    sector_t capacity;
    struct blk_mq_tag_set tag_set;
    struct request_queue *queue;
    struct gendisk *gdisk;
    struct BmpStorage *bmpS;

    struct SteganographyBlockDevice *pnext;
};

struct SbdWorker {
    struct work_struct work;
    struct request *rq;
};

//// bmp

#define COLORS_PER_PIXEL 4
#define USED_BITS_PER_PIXEL 2
#define BMP_HEADER_SIZE 54
#define BMP_IDX_OFFSET 6
#define BMP_COUNT_OFFSET 8

struct Bmp {
    struct file *fd;
    ulong size;
    uint16 idx;

    uint width;
    uint height;

    uint headerSize;
    uint rowSize;
    uint8 padding;

    ulong virtualSize;
    ulong virtualOffset;

    struct Bmp* pnext;
};

struct BmpStorage {
    struct Bmp *bmps;
    uint16 count;
    ulong totalVirtualSize;
    char* backingPath;
};
