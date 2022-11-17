#include "main.h"

// controller
static struct SteganographyControlDevice *ctlDev = NULL;

//// add and remove devices

char getNextAvailableLetter(void) {
    char letter = 'a';
    while (letter <= 'z') {
        struct SteganographyBlockDevice *dev = ctlDev->pnext;
        while (dev != NULL) {
            if (dev->letter == letter) {
                break;
            }
            dev = dev->pnext;
        }
        if (dev == NULL) {
            return letter;
        }
        letter++;
    }
    return 0;
}

int addDev(char* backingPath, char** name) {
    int err = 0;
    struct SteganographyBlockDevice *dev = NULL;
    struct SteganographyBlockDevice *pprev = NULL;
    
    printInfo("!!! add device\n");

    if (!backingPath) {
        printError("backingPath not provided\n");
        err = -ENOENT;
        goto noBackingPath;
    }

    dev = kmalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (dev == NULL) {
        printError("failed to allocate dev struct\n");
        err = -ENOMEM;
        goto failedAllocdev;
    }

    dev->letter = getNextAvailableLetter();
    if (dev->letter == 0) {
        printError("no available letter\n");
        err = -ENOMEM;
        goto failedAllocLetter;
    }

    *name = kmalloc(sizeof (char) * strlen(BLK_DEV_NAME) + 1 + 1, GFP_KERNEL);
    if (*name == NULL) {
        printError("failed to allocate memory for name\n");
        err = -ENOMEM;
        goto failedAllocName;
    }
    sprintf(*name, "%s%c", BLK_DEV_NAME, dev->letter);

    // register new block device and get device major number
    dev->devMajor = register_blkdev(0, *name);
    if (dev->devMajor < 0) {
        printError("failed to register block device\n");
        err = dev->devMajor;
        goto failedRegisterBlkDev;
    }

    // allocate queue
    if (blk_mq_alloc_sq_tag_set(&dev->tag_set, &mqOps, 128, BLK_MQ_F_SHOULD_MERGE)) {
        printError("failed to allocate device queue\n");
        err = -ENOMEM;
        goto failedAllocQueue;
    }

    // allocate gdisk
    dev->gdisk = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (dev->gdisk == NULL) {
        printError("failed to allocate gdisk\n");
        err = -ENOMEM;
        goto failedAllocGdisk;
    }

    // set all required flags and data
    dev->gdisk->flags = GENHD_FL_NO_PART;
    dev->gdisk->major = dev->devMajor; // todo: duplicates
    dev->gdisk->minors = 1; // any number
    dev->gdisk->first_minor = 0;

    dev->gdisk->fops = &bdOps;
    dev->gdisk->private_data = dev;

    // set device name as it will be represented in /dev
    strncpy(dev->gdisk->disk_name, *name + 0, 9);
    printInfo("adding disk /dev/%s\n", dev->gdisk->disk_name);

    // open backing files
    dev->bmpS = kmalloc(sizeof(struct BmpStorage), GFP_KERNEL);
    if (dev->bmpS == NULL) {
        printError("failed to allocate dev->bmpS struct\n");
        err = -ENOMEM;
        goto failedAllocBmpS;
    }
    dev->bmpS->backingPath = backingPath;
    if (( err = openBmps(dev->bmpS) )) {
        printError("failed to open backing files\n");
        goto failedOpenBmps;
    }

    // set device capacity
    dev->capacity = dev->bmpS->totalVirtualSize / SECTOR_SIZE;
    if(dev->capacity == 0) {
        printError("capacity is 0\n");
        err = -EINVAL;
        goto failedCapacity;
    }
    set_capacity(dev->gdisk, dev->capacity);
    printInfo("sector size: %d B * capacity: %llu sectors = %llu B \n", SECTOR_SIZE, dev->capacity, dev->capacity * SECTOR_SIZE);

    // notify kernel about new disk device
    if(( err = add_disk(dev->gdisk) )) {
        printError("Failed to add disk\n");
        goto failedToAdd;
    }

    if(ctlDev->pnext == NULL) {
        ctlDev->pnext = dev;
    } else {
        pprev = ctlDev->pnext;
        while(pprev->pnext != NULL) {
            pprev = pprev->pnext;
        }
        pprev->pnext = dev;
    }

    return 0;

failedToAdd:
failedCapacity:
    closeBmps(dev->bmpS); // undo openBmps
failedOpenBmps:
    kfree(dev->bmpS); // undo kmalloc bmpS
failedAllocBmpS:
    put_disk(dev->gdisk); // undo blk_mq_alloc_disk
failedAllocGdisk:
    blk_mq_free_tag_set(&dev->tag_set); // undo blk_mq_alloc_sq_tag_set
failedAllocQueue:
    unregister_blkdev(dev->devMajor, *name); // undo register_blkdev
failedRegisterBlkDev:
    kfree(dev); // undo kmalloc dev
    kfree(*name); // undo kmalloc name
failedAllocName:
failedAllocLetter:
failedAllocdev:
    kfree(backingPath); // undo kmalloc backingPath in parent function
noBackingPath:
    printError("addDev() failed with error %d", err);
    return err;
}

int removeDev(struct SteganographyBlockDevice *dev) {
    // todo: cleanup queue?

    printInfo("removing disk /dev/%s\n", dev->gdisk->disk_name);

    if(dev->bmpS) {
        closeBmps(dev->bmpS);
        kfree(dev->bmpS);
    }
    if(dev->gdisk) {
        del_gendisk(dev->gdisk);
        put_disk(dev->gdisk);
    } else {
        printError("dev->gdisk is NULL\n");
    }
    kfree(dev->bmpS->backingPath);
    unregister_blkdev(dev->devMajor, dev->gdisk->disk_name); // todo: maybe do it first to prevent new requests?
    kfree(dev);
    return 0;
}

int findRemoveDev(char* backingPath) {
    struct SteganographyBlockDevice *pprev = NULL;
    struct SteganographyBlockDevice *dev = ctlDev->pnext; // todo: single line?

    printInfo("!!! remove device\n");

    while(dev != NULL) {
        if(strcmp(dev->bmpS->backingPath, backingPath) == 0) {
            if(pprev == NULL)
                ctlDev->pnext = dev->pnext;
            else
                pprev->pnext = dev->pnext;
            
            removeDev(dev);
            return 0;
        }
        pprev = dev;
        dev = dev->pnext;
    }
    printError("device %s not found\n", backingPath);
    return 1;
}

//// block device operations

static int devOpen(struct block_device *bd, fmode_t mode) {
    return 0;
}

static void devRelease(struct gendisk *gdisk, fmode_t mode) {
    return;
}

int devIoCtl(struct block_device *bd, fmode_t mode, uint cmd, ulong arg) {
    int err = 0;
    int copied;
    char* backingPath;

    if(strcmp(bd->bd_disk->disk_name, CTL_DEV_NAME) != 0) {
        printError("ioctl only supported for control device\n");
        return -EINVAL;
    }

    if(arg == 0) {
        printError("arg is NULL\n");
        return -EINVAL;
    }

    backingPath = kmalloc(MAX_BACKING_LEN, GFP_KERNEL);
    if(backingPath == NULL) {
        printError("failed to allocate memory for backingPath\n");
        return -ENOMEM;
    }

    copied = copy_from_user(backingPath, (char*)arg, MAX_BACKING_LEN);
    if(copied < 0) {
        printError("copy_from_user failed\n");
        err = -EINVAL;
        goto failedCopyFromUser;
    }
    if(copied == MAX_BACKING_LEN) {
        printError("backingPath too long\n");
        err = -EINVAL;
        goto failedCopyFromUser;
    }

    if (cmd == IOCTL_DEV_ADD) {
        char* name;
        err = addDev(backingPath, &name);
        name[4] = '\0';
        if(err) return err;
        copied = copy_to_user((char*)arg, name, MAX_BACKING_LEN);
        if(copied < 0) {
            printError("copy_to_user failed\n");
            err = -EINVAL;
            goto failedCopyFromUser;
        }
        if(copied == MAX_BACKING_LEN) {
            printError("name too long\n");
            err = -EINVAL;
            goto failedCopyFromUser;
        }
        return 0;
    } else if(cmd == IOCTL_DEV_REMOVE) {
        return findRemoveDev(backingPath);
    } else {
        printError("unknown ioctl command %d", cmd);
        err = -EINVAL;
    }
failedCopyFromUser:
    kfree(backingPath);
    return err;
}

// set block device file I/O
static struct block_device_operations bdOps = {
    .owner = THIS_MODULE,
    .open = devOpen,
    .release = devRelease,
    .ioctl = devIoCtl
};

//// worker

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
        printError("failed to allocate worker struct");
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

static int __init moduleInit(void) {
    int err = 0;
    printInfo("!!! module initialize\n");

    ctlDev = kmalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (ctlDev == NULL) {
        printError("failed to allocate dev struct\n");
        err = -ENOMEM;
        goto failedAllocdev;
    }

    // register new block device and get device major number
    ctlDev->devMajor = register_blkdev(0, CTL_DEV_NAME);
    if (ctlDev->devMajor < 0) {
        printError("failed to register control device\n");
        err = ctlDev->devMajor;
        goto failedRegisterBlkDev;
    }

    // allocate queue
    if (blk_mq_alloc_sq_tag_set(&ctlDev->tag_set, &mqOps, 128, BLK_MQ_F_SHOULD_MERGE)) {
        printError("failed to allocate device queue\n");
        err = -ENOMEM;
        goto failedAllocQueue;
    }

    // allocate gdisk
    ctlDev->gdisk = blk_mq_alloc_disk(&ctlDev->tag_set, ctlDev);
    if (ctlDev->gdisk == NULL) {
        printError("failed to allocate gdisk\n");
        err = -ENOMEM;
        goto failedAllocGdisk;
    }

    // set all required flags and data
    ctlDev->gdisk->flags = GENHD_FL_NO_PART;
    ctlDev->gdisk->major = ctlDev->devMajor; // todo: duplicates
    ctlDev->gdisk->minors = 1; // any number
    ctlDev->gdisk->first_minor = 0;

    ctlDev->gdisk->fops = &bdOps;
    ctlDev->gdisk->private_data = ctlDev;

    // set device name as it will be represented in /dev
    strncpy(ctlDev->gdisk->disk_name, CTL_DEV_NAME, 11);
    printInfo("adding disk /dev/%s\n", ctlDev->gdisk->disk_name);

    ctlDev->bmpS = NULL;

    // set device capacity
    ctlDev->capacity = 0;
    set_capacity(ctlDev->gdisk, ctlDev->capacity);

    // notify kernel about new disk device
    if(( err = add_disk(ctlDev->gdisk) )) {
        printError("Failed to add disk\n");
        goto failedToAdd;
    }

    ctlDev->pnext = NULL;

    return 0;

failedToAdd:
    put_disk(ctlDev->gdisk); // undo blk_mq_alloc_disk
failedAllocGdisk:
    blk_mq_free_tag_set(&ctlDev->tag_set); // undo blk_mq_alloc_sq_tag_set
failedAllocQueue:
    unregister_blkdev(ctlDev->devMajor, CTL_DEV_NAME); // undo register_blkdev
failedRegisterBlkDev:
    kfree(ctlDev); // undo kmalloc dev
failedAllocdev:
    printError("devInit() failed with error %d", err);
    return err;
}

// release disk and free memory
static void __exit moduleExit(void) {
    struct SteganographyBlockDevice *dev = ctlDev->pnext;
    printInfo("!!! module exit\n");
    while(dev) {
        struct SteganographyBlockDevice *next = dev->pnext;
        removeDev(dev);
        dev = next;
    }
    
    // todo: cleanup queue?
    // closeBmps(ctlDev->bmpS);
    // kfree(ctlDev->bmpS);
    del_gendisk(ctlDev->gdisk);
    put_disk(ctlDev->gdisk);
    unregister_blkdev(ctlDev->devMajor, CTL_DEV_NAME); // todo: maybe do it first to prevent new requests?
    kfree(ctlDev);
}

module_init(moduleInit);
module_exit(moduleExit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tymoteusz Dolega");
