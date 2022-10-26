#include "stg.h"

// path of backing files
char* backingPath = NULL;
// device instance
static struct SteganographyBlockDevice *sbd = NULL;

//// block device operations

static int devOpen(struct block_device *bd, fmode_t mode) {
    return 0;
}

static void devRelease(struct gendisk *gdisk, fmode_t mode) {
    return;
}

int devIoCtl(struct block_device *bd, fmode_t mode, uint cmd, ulong arg) {
    return -ENOTTY;
}

// set block device file I/O
static struct block_device_operations bdOps = {
    .owner = THIS_MODULE,
    .open = devOpen,
    .release = devRelease,
    .ioctl = devIoCtl
};

//// worker

struct SbdWorker {
    struct work_struct work;
    struct request *rq;
};

// serve requests
static int requestHandler(struct request *rq, ulong *nrBytes) {
    int err = 0;
    struct bio_vec bvec;
    struct req_iterator iter;
    struct SteganographyBlockDevice *dev = rq->q->queuedata;
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;

    // iterate over all requests segments
    rq_for_each_segment(bvec, rq, iter) {
        // get pointer to the data
        void* bBuf = page_address(bvec.bv_page) + bvec.bv_offset;
        ulong bLen = bvec.bv_len;

        if (rq_data_dir(rq) == WRITE)
            err = bsEncode(bBuf, bLen, pos, dev->bmpS);
        else
            err = bsDecode(bBuf, bLen, pos, dev->bmpS);
        
        if (err) return err;

        pos += bLen;
        *nrBytes += bLen;
    }

    return err;
}


static void requestHandlerThread(struct work_struct *work_arg){
    struct SbdWorker *worker = container_of(work_arg, struct SbdWorker, work);
    struct request *rq = worker->rq;
    ulong nrBytes = 0; // todo: use it
    requestHandler(rq, &nrBytes); // todo: handle return value

    blk_mq_complete_request(rq);

    kfree(worker);
}

//// blk_mq_ops

static blk_status_t queueRq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd) {
    struct request *rq = bd->rq;
    struct SbdWorker *worker = kmalloc(sizeof(struct SbdWorker), GFP_KERNEL);
    if (worker == NULL) {
        printError("cannot allocate worker");
        return -ENOMEM;
    }

    blk_mq_start_request(rq);
    worker->rq = rq;
    INIT_WORK(&worker->work, requestHandlerThread);
    schedule_work(&worker->work);

    return BLK_STS_OK;
}

static void completeRq(struct request *rq) {
    blk_mq_end_request(rq, BLK_STS_OK); // yolo
}

static struct blk_mq_ops mqOps = {
    .queue_rq = queueRq,
    .complete = completeRq,
};

//// init && exit

static int __init sbdInit(void) {
    int err = 0;
    printInfo("!!! sbd initialize\n");

    if (!backingPath) {
        printError("backingPath not provided\n");
        err = -ENOENT;
        goto noBackingPath;
    }

    sbd = kmalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (sbd == NULL) {
        printError("failed to allocate struct sbd\n");
        err = -ENOMEM;
        goto failedAllocSbd;
    }

    // register new block device and get device major number
    sbd->devMajor = register_blkdev(0, BLK_DEV_NAME);
    if (sbd->devMajor < 0) {
        printError("failed to register block device\n");
        err = sbd->devMajor;
        goto failedRegisterBlkDev;
    }

    // allocate queue
    if (blk_mq_alloc_sq_tag_set(&sbd->tag_set, &mqOps, 128, BLK_MQ_F_SHOULD_MERGE)) {
        printError("failed to allocate device queue\n");
        err = -ENOMEM;
        goto failedAllocQueue;
    }

    // allocate disk (?)
    sbd->gdisk = blk_mq_alloc_disk(&sbd->tag_set, sbd);
    if (sbd->gdisk == NULL) {
        printError("failed to allocate gdisk\n");
        err = -ENOMEM;
        goto failedAllocGdisk;
    }

    // set all required flags and data
    sbd->gdisk->flags = GENHD_FL_NO_PART;
    sbd->gdisk->major = sbd->devMajor; // todo: duplicates
    sbd->gdisk->minors = 1; // any number
    sbd->gdisk->first_minor = 0;

    sbd->gdisk->fops = &bdOps;
    sbd->gdisk->private_data = sbd;

    // set device name as it will be represented in /dev
    strncpy(sbd->gdisk->disk_name, BLK_DEV_NAME + 0, 9);
    printInfo("adding disk /dev/%s\n", sbd->gdisk->disk_name);

    // open backing files
    sbd->bmpS = kmalloc(sizeof(struct BmpStorage), GFP_KERNEL);
    if (sbd->bmpS == NULL) {
        printError("failed to allocate struct sbd->bmpS\n");
        err = -ENOMEM;
        goto failedAllocBmpS;
    }
    sbd->bmpS->backingPath = backingPath;
    if (( err = openBmps(sbd->bmpS) )) {
        printError("failed to open backing files\n");
        goto failedOpenBmps;
    }

    // set device capacity
    sbd->capacity = sbd->bmpS->totalVirtualSize / SECTOR_SIZE;
    if(sbd->capacity == 0) {
        printError("capacity is 0\n");
        err = -EINVAL;
        goto failedCapacity;
    }
    set_capacity(sbd->gdisk, sbd->capacity);
    printInfo("sector size: %d B * capacity: %llu sectors = %llu B \n", SECTOR_SIZE, sbd->capacity, sbd->capacity * SECTOR_SIZE);

    // notify kernel about new disk device
    if(( err = add_disk(sbd->gdisk) )) {
        printError("Failed to add disk\n");
        goto failedToAdd;
    }

    return 0;

failedToAdd:
failedCapacity:
    closeBmps(sbd->bmpS); // undo openBmps
failedOpenBmps:
    kfree(sbd->bmpS); // undo kmalloc bmpS
failedAllocBmpS:
    put_disk(sbd->gdisk); // undo blk_mq_alloc_disk
failedAllocGdisk:
    blk_mq_free_tag_set(&sbd->tag_set); // undo blk_mq_alloc_sq_tag_set
failedAllocQueue:
    unregister_blkdev(sbd->devMajor, BLK_DEV_NAME); // undo register_blkdev
failedRegisterBlkDev:
    kfree(sbd); // undo kmalloc sbd
failedAllocSbd:
// failedReadBackingPath:
noBackingPath:
    printError("sbdInit() failed with error %d", err);
    return err;
}

// release disk and free memory
static void __exit sbdExit(void) {
    printInfo("!!! sbd destroy\n");
    // todo: cleanup queue?
    closeBmps(sbd->bmpS);
    // todo delete bmps
    kfree(sbd->bmpS);
    del_gendisk(sbd->gdisk);
    put_disk(sbd->gdisk);
    unregister_blkdev(sbd->devMajor, BLK_DEV_NAME); // todo: maybe do it first to prevent new requests?
    kfree(sbd);
}

module_init(sbdInit);
module_exit(sbdExit);
module_param(backingPath, charp, 0);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tymoteusz Dolega");
