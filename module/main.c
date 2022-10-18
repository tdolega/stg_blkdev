#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>

#include <asm/uaccess.h>
#include <linux/kernel.h>

#include <linux/workqueue.h>

#include "stg.h"

#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

#define BLK_DEV_NAME "sbd"

int err = 0;
char* backingPath = NULL; // TODO: move to the struct?

// char *filePaths[] = {"/tmp/bmps/1.bmp", "/tmp/bmps/3.bmp", "/tmp/bmps/2.bmp", "/tmp/bmps/32b_rgba.bmp"};
char *filePaths[] = {"/tmp/bmps/32b_rgba.bmp"};
// internal representation of our block device, can hold any useful data
struct SteganographyBlockDevice {
    sector_t capacity;
    // u8 *data;   // data buffer to emulate real storage device
    struct blk_mq_tag_set tag_set;
    struct request_queue *queue;
    struct gendisk *gdisk;
    // struct file *fd;
    struct BmpStorage *bmpS;
};
// device instance
static struct SteganographyBlockDevice *sbd = NULL;
static int devMajor = 0;

//// block_device_operations

static int blockdevOpen(struct block_device *bd, fmode_t mode) {
//    printInfo("blockdevOpen()\n");
    return 0;
}

static void blockdevRelease(struct gendisk *gdisk, fmode_t mode) {
//    printInfo("blockdevRelease()\n");
}

int blockdevIoctl(struct block_device *bd, fmode_t mode, unsigned cmd, unsigned long arg) {
//    printInfo("blockdevIoctl()\n");
    return -ENOTTY;
}

// set block device file I/O
static struct block_device_operations bdOps = {
    .owner = THIS_MODULE,
    .open = blockdevOpen,
    .release = blockdevRelease,
    .ioctl = blockdevIoctl
};

//// worker

struct SbdWorker {
    struct work_struct work;
    struct request *rq;
    unsigned int rqIdx;
};

#define wqSize 10000
struct SbdWorker *wq[wqSize];
int wqHead = 0;

// serve requests
static int doRequest(struct request *rq, unsigned int *nr_bytes) {
    int ret = 0;
    struct bio_vec bvec;
    struct req_iterator iter;
    struct SteganographyBlockDevice *dev = rq->q->queuedata;
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
    loff_t dev_size = (loff_t)(dev->capacity << SECTOR_SHIFT);

    // printInfo("sblkdev: request start from sector %lld  pos = %lld  dev_size = %lld\n", blk_rq_pos(rq), pos, dev_size);

    // iterate over all requests segments
    rq_for_each_segment(bvec, rq, iter) {
        unsigned long b_len = bvec.bv_len;

        /* Get pointer to the data */
        void* b_buf = page_address(bvec.bv_page) + bvec.bv_offset;

        /* Simple check that we are not out of the memory bounds */
        if ((pos + b_len) > dev_size) {
            b_len = (unsigned long)(dev_size - pos);
        }

        if (rq_data_dir(rq) == WRITE) {
//            memcpy(dev->data + pos, b_buf, b_len);
            // kernel_write(dev->fd, b_buf, b_len, &pos);
            bsEncode(b_buf, b_len, pos, dev->bmpS);
        } else {
//            memcpy(b_buf, dev->data + pos, b_len);
            // kernel_read(dev->fd, b_buf, b_len, &pos);
            bsDecode(b_buf, b_len, pos, dev->bmpS);
        }

//        pos += b_len;
        *nr_bytes += b_len;
    }

    return ret; // todo: ret is ignored right now
}


static void thread_function(struct work_struct *work_arg){
    struct SbdWorker *c_ptr = container_of(work_arg, struct SbdWorker, work);
    unsigned int nr_bytes = 0; // todo: use it

    struct request *rq = c_ptr->rq;
    doRequest(rq, &nr_bytes);

    blk_mq_complete_request(rq);
}

//// blk_mq_ops

static blk_status_t queueRq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd) {
    struct request *rq = bd->rq;
    struct SbdWorker *worker = kmalloc(sizeof(struct SbdWorker), GFP_KERNEL);

    if(!worker) {
        printError("cannot allocate worker");
        return -ENOMEM;
    }

    blk_mq_start_request(rq);

    INIT_WORK(&worker->work, thread_function);
    worker->rqIdx = wqHead;
    worker->rq = rq;
    schedule_work(&worker->work);

    wq[wqHead] = worker;

    wqHead++;
    if(wqHead >= wqSize) {
        wqHead = 0;
        printError("wq looped\n");
    }

    return BLK_STS_OK;
}

static void completeRq(struct request *rq) {
    // yolo
    blk_mq_end_request(rq, BLK_STS_OK);
}

static struct blk_mq_ops mqOps = {
    .queue_rq = queueRq,
    .complete = completeRq,
};

//// init && exit

static int __init sbdInit(void) {
    printInfo("sbdInit()\n");

    if(!backingPath) {
        printError("backingPath not provided\n");
        return -ENOENT;
    }

    // register new block device and get device major number
    devMajor = register_blkdev(devMajor, BLK_DEV_NAME);

    sbd = kmalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (sbd == NULL) {
        printError("Failed to allocate struct block_dev\n");
        err = -ENOMEM;
        goto failedToRegister;
    }

    // sbd->fd = filp_open(backingPath, O_RDWR, 0644);
    // if(IS_ERR_OR_NULL(sbd->fd)) {
    //     printError("Failed to open backingPath\n");
    //     err = -1; // todo: different error
    //     goto failedToCreate;
    // }

    // allocate corresponding data buffer
    // sbd->data = vmalloc(sbd->capacity << 9);
    // if (sbd->data == NULL) {
    //     printError("Failed to allocate device IO buffer\n");
    //     goto failedToCreate;
    // }

    // allocate queue
    if (blk_mq_alloc_sq_tag_set(&sbd->tag_set, &mqOps, 128, BLK_MQ_F_SHOULD_MERGE)) {
        printError("Failed to allocate device queue\n");
        err = -ENOMEM;
        goto failedToAllocateQueue;
    }

    // allocate disk  TODO: what does it mean?
    sbd->gdisk = blk_mq_alloc_disk(&sbd->tag_set, sbd);
    if(sbd->gdisk == NULL) {
        printError("Failed to allocate disk\n");
        err = -ENOMEM;
        goto failedToAllocateDisk;
    }

    // set all required flags and data
    // sbd->gdisk->flags = GENHD_FL_NO_PART;
    sbd->gdisk->flags = GENHD_FL_NO_PART_SCAN;
    sbd->gdisk->major = devMajor;
    sbd->gdisk->minors = 1; // TODO: guessed number
    sbd->gdisk->first_minor = 0;

    sbd->gdisk->fops = &bdOps;
    sbd->gdisk->private_data = sbd;

    // set device name as it will be represented in /dev
    strncpy(sbd->gdisk->disk_name, BLK_DEV_NAME + 0, 9);

    printInfo("Adding disk %s\n", sbd->gdisk->disk_name);

    // open backing files
    sbd->bmpS = kmalloc(sizeof(struct BmpStorage), GFP_KERNEL);
    if (sbd->bmpS == NULL) {
        printError("Failed to allocate struct bmpS\n");
        err = -ENOMEM;
        goto failedToOpenBackingFiles;
    }
    sbd->bmpS->count = sizeof(filePaths) / sizeof(char *);
    sbd->bmpS->bmps = kmalloc(sizeof(struct Bmp) * sbd->bmpS->count, GFP_KERNEL);
    if (sbd->bmpS->bmps == NULL) {
        printError("Failed to allocate bmps structs\n");
        err = -ENOMEM;
        goto failedToAllocateBmpsStructs;
    }
    if (openBmps(filePaths, sbd->bmpS)) {
        printError("Failed to open backing files\n");
        err = -1; // todo: different error
        goto failedToOpenBmps;
    }

    // set device capacity
    sbd->capacity = sbd->bmpS->totalVirtualSize / SECTOR_SIZE;
    set_capacity(sbd->gdisk, sbd->capacity);

    // notify kernel about new disk device
    if(( err = add_disk(sbd->gdisk) )) {
        printError("Failed to add disk\n");
        goto failedToAdd;
    }

    return 0;

failedToAdd:
    kfree(sbd->bmpS->bmps);
failedToOpenBmps:
failedToAllocateBmpsStructs:
    kfree(sbd->bmpS);
failedToOpenBackingFiles:
    blk_cleanup_disk(sbd->gdisk);
    // put_disk(sbd->gdisk);
failedToAllocateDisk:
    blk_mq_free_tag_set(&sbd->tag_set);
failedToAllocateQueue:
    // vfree(sbd->data);
// failedToCreate:
    // kfree(sbd);
failedToRegister:
    unregister_blkdev(devMajor, BLK_DEV_NAME);
    return err;
}

// release disk and free memory
static void __exit sbdExit(void) {
    printInfo("sbdExit()\n");

    if (sbd->gdisk) {
        del_gendisk(sbd->gdisk);
        blk_cleanup_disk(sbd->gdisk);
        // put_disk(sbd->gdisk);
    }

    // filp_close(sbd->fd, NULL);

    unregister_blkdev(devMajor, BLK_DEV_NAME);
    // vfree(sbd->data);
    closeBmps(sbd->bmpS);
    kfree(sbd);
}

module_init(sbdInit);
module_exit(sbdExit);
module_param(backingPath, charp, 0);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tymoteusz Dolega");
