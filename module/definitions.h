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
/// everything above is copied

#define uint8 unsigned char
#define uint unsigned int
#define ulong unsigned long

#define COLORS_PER_PIXEL 4
#define BMP_HEADER_SIZE 54

struct Bmp {
    struct file *fd;
    long size;
    char* path;

    uint width;
    uint height;

    uint headerSize;
    uint rowSize;
    uint padding;

    ulong virtualSize;
    ulong virtualOffset;

    void* filesim;
};

struct BmpStorage {
    struct Bmp *bmps;
    uint count;
    ulong totalVirtualSize;
};

/////

#define printInfo(...)  printk(KERN_INFO "stg_blkdev: " __VA_ARGS__)
#define printError(...) printk(KERN_ERR  "stg_blkdev: " __VA_ARGS__)

/////

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')
