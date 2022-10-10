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
/// everything above is copied

#include "definitions.h"

uint openBmps(char **filePaths, struct BmpStorage *bmpS);
void closeBmps(struct BmpStorage *bmpS);

int bsEncode(const uint8 *data, uint size, loff_t position, struct BmpStorage *bmpS);
int bsDecode(uint8 *data, uint size, loff_t position, struct BmpStorage *bmpS);
