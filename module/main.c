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
    struct SteganographyBlockDevice *dev = ctlDev->pnext;
    
    printInfo("!!! add device\n");

    if (!backingPath) {
        printError("backingPath not provided\n");
        err = -ENOENT;
        goto noBackingPath;
    }

    while(dev != NULL) {
        if (strcmp(dev->bmpS->backingPath, backingPath) == 0) {
            printError("device with backingPath %s already exists\n", backingPath);
            err = -EEXIST;
            goto backingPathExists;
        }
        dev = dev->pnext;
    }

    dev = kzalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (dev == NULL) {
        printError("failed to allocate dev struct\n");
        err = -ENOMEM;
        goto failedAllocDev;
    }
    dev->pnext = NULL;

    // open backing files
    dev->bmpS = kzalloc(sizeof(struct BmpStorage), GFP_KERNEL);
    if (dev->bmpS == NULL) {
        printError("failed to allocate dev->bmpS struct\n");
        err = -ENOMEM;
        goto failedAllocBmpS;
    }
    dev->bmpS->backingPath = backingPath;

    printDebug("opening backing files\n");
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
    printInfo("sector size: %d B * capacity: %llu sectors = available: %llu B \n", SECTOR_SIZE, dev->capacity, dev->capacity * SECTOR_SIZE);

    dev->letter = getNextAvailableLetter();
    if (dev->letter == 0) {
        printError("no available letter\n");
        err = -ENOMEM;
        goto failedAllocLetter;
    }

    *name = kzalloc(sizeof (char) * strlen(BLK_DEV_NAME) + 1 + 1, GFP_KERNEL);
    if (*name == NULL) {
        printError("failed to allocate memory for name\n");
        err = -ENOMEM;
        goto failedAllocName;
    }
    sprintf(*name, "%s%c", BLK_DEV_NAME, dev->letter);

    // register new block device and get device major number
    printDebug("registering block device %s", *name);
    dev->devMajor = register_blkdev(0, *name);
    if (dev->devMajor < 0) {
        printError("failed to register block device\n");
        err = dev->devMajor;
        goto failedRegisterBlkDev;
    }

    // allocate queue
    printDebug("allocating queue");
    if (blk_mq_alloc_sq_tag_set(&dev->tag_set, &mqOps, 128, BLK_MQ_F_SHOULD_MERGE)) {
        printError("failed to allocate device queue\n");
        err = -ENOMEM;
        goto failedAllocQueue;
    }

    // allocate gdisk
    printDebug("allocating gdisk");
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

    printDebug("setting capacity");
    set_capacity(dev->gdisk, dev->capacity);

    // set device name as it will be represented in /dev
    strncpy(dev->gdisk->disk_name, *name + 0, 9);
    printInfo("adding disk /dev/%s\n", dev->gdisk->disk_name);

    // notify kernel about new disk device
    printDebug("adding disk");
    if(( err = add_disk(dev->gdisk) )) {
        printError("Failed to add disk\n");
        goto failedToAdd;
    }

    if(ctlDev->pnext == NULL) {
        ctlDev->pnext = dev;
    } else {
        struct SteganographyBlockDevice *pprev = ctlDev->pnext;
        while(pprev->pnext != NULL) {
            pprev = pprev->pnext;
        }
        pprev->pnext = dev;
    }

    return 0;

failedToAdd:
    printDebug("del_gendisk");
    del_gendisk(dev->gdisk); // somehow avoid unexpected crashing with next disk
    printDebug("put_disk");
    put_disk(dev->gdisk); // undo blk_mq_alloc_disk

failedAllocGdisk:
    printDebug("blk_mq_free_tag_set");
    blk_mq_free_tag_set(&dev->tag_set); // undo blk_mq_alloc_sq_tag_set // TODO: is this the cause of the errors?

failedAllocQueue:
    printDebug("unregister_blkdev");
    unregister_blkdev(dev->devMajor, *name); // undo register_blkdev

failedRegisterBlkDev:
    printDebug("kfree *name");
    kfree(*name); // undo kmalloc name

failedAllocName:
failedAllocLetter:
failedCapacity:
    printDebug("closeBmps");
    closeBmps(dev->bmpS); // undo openBmps

failedOpenBmps:
    printDebug("kfree dev->bmpS");
    kfree(dev->bmpS); // undo kmalloc bmpS

failedAllocBmpS:
    printDebug("kfree dev");
    kfree(dev); // undo kmalloc dev

failedAllocDev:
    printDebug("kfree backingPath");
    kfree(backingPath); // undo kmalloc backingPath in parent function

backingPathExists:
noBackingPath:
    printError("device will not be created (error %d)", err);
    return err;
}

int removeDev(struct SteganographyBlockDevice *dev) {
    printInfo("removing disk /dev/%s\n", dev->gdisk->disk_name);

    if(dev->gdisk) {
        printDebug("del_gendisk");
        del_gendisk(dev->gdisk);
    } else {
        printError("dev->gdisk is NULL #1\n");
    }

    printDebug("blk_mq_free_tag_set");
    blk_mq_free_tag_set(&dev->tag_set);

    if(dev->gdisk) {
        printDebug("unregister_blkdev");
        unregister_blkdev(dev->devMajor, dev->gdisk->disk_name);
    } else { 
        printError("dev->gdisk is NULL #2\n");
    }

    if(dev->gdisk) {
        printDebug("put_disk");
        put_disk(dev->gdisk);
    } else {
        printError("dev->gdisk is NULL #3\n");
    }

    if(dev->bmpS) {
        printDebug("closeBmps");
        closeBmps(dev->bmpS);

        if(dev->bmpS->backingPath) {
            printDebug("kfree dev->bmpS->backingPath");
            kfree(dev->bmpS->backingPath);
        }

        printDebug("kfree dev->bmpS");
        kfree(dev->bmpS);
    }

    printDebug("kfree dev");
    kfree(dev);

    return 0;
}

int findRemoveDev(char* deviceName) {
    struct SteganographyBlockDevice *pprev = NULL;
    struct SteganographyBlockDevice *dev = ctlDev->pnext;
    char letter = 0;

    printInfo("!!! remove device\n");

    if(strlen(deviceName) != 4 || deviceName[0] != 's' || deviceName[1] != 't' || deviceName[2] != 'g') {
        printError("invalid device name: %s\n", deviceName);
        return 1;
    }
    letter = deviceName[3];

    while(dev != NULL) {
        if(dev->letter == letter) {
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
    printError("device %s not found\n", deviceName);
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
        printError("%s: ioctl request not supported\n", bd->bd_disk->disk_name);
        return -EINVAL;
    }

    if(arg == 0) {
        printError("arg is NULL\n");
        return -EINVAL;
    }

    backingPath = kzalloc(MAX_BACKING_LEN, GFP_KERNEL);
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
        if(err) return err;
        name[4] = '\0';
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
    struct SbdWorker *worker = kmalloc(sizeof(struct SbdWorker), GFP_KERNEL); // todo: use kzalloz if encounter problems
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

    ctlDev = kzalloc(sizeof (struct SteganographyBlockDevice), GFP_KERNEL);
    if (ctlDev == NULL) {
        printError("failed to allocate dev struct\n");
        err = -ENOMEM;
        goto failedAllocdev;
    }
    ctlDev->pnext = NULL;

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

    printInfo("added control device");

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
    printInfo("removing all devices");
    while(dev != NULL) {
        struct SteganographyBlockDevice *next = dev->pnext;
        removeDev(dev);
        dev = next;
    }
    
    printInfo("removing control device");
    printDebug("del_gendisk");
    del_gendisk(ctlDev->gdisk);
    printDebug("blk_mq_free_tag_set");
    blk_mq_free_tag_set(&ctlDev->tag_set);
    printDebug("unregister_blkdev");
    unregister_blkdev(ctlDev->devMajor, CTL_DEV_NAME);
    printDebug("put_disk");
    put_disk(ctlDev->gdisk);
    printDebug("kfree ctlDev");
    kfree(ctlDev);
}

module_init(moduleInit);
module_exit(moduleExit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tymoteusz Dolega");
