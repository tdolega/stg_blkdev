#include "stg.h"

void bRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    kernel_read(bmp->fd, buffer, size, &position);
}

void bWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    kernel_write(bmp->fd, buffer, size, &position);
}

ulong pixelIdxToBmpIdx(struct Bmp *bmp, ulong pixel) {
    uint row = pixel / bmp->width;
    uint col = pixel % bmp->width;
    return (ulong) row * bmp->rowSize + col * COLORS_PER_PIXEL + bmp->headerSize;
}

void bDecode(uint8 *data, ulong size, loff_t position, struct Bmp *bmp) {
    for (ulong byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = 0;
        ulong pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (uint8 colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color, twoBits;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = color & 0b00000011;
            byte |= twoBits << (colorIdx * USED_BITS_PER_PIXEL);
        }
        data[byteIdx] = byte;
    }
}

void bEncode(uint8 *data, ulong size, loff_t position, struct Bmp *bmp) {
    for (ulong byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = data[byteIdx];
        ulong pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (uint8 colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color, twoBits;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = (byte >> (colorIdx * USED_BITS_PER_PIXEL)) & 0b11;
            color = (color & 0b11111100) | twoBits;
            bWrite(&color, 1, pixelIdx + colorIdx, bmp);
        }
    }
}

int bsXncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS, xncoder_t xncoder) {
    struct Bmp *bmp = bmpS->bmps;
    
    if (position + size > bmpS->totalVirtualSize) {
        printError("not enough space\n");
        return -ENOSPC; // instead of erroring out, truncating is also a possibility...
    }

    while (position >= bmp->virtualSize) {
        position -= bmp->virtualSize;
        bmp = bmp->pnext;
    }

    while (size > 0) {
        ulong posToEnd = bmp->virtualSize - position;
        ulong bytesToXncode = posToEnd < size ? posToEnd : size;

        xncoder(data, bytesToXncode, position, bmp);

        data += bytesToXncode;
        size -= bytesToXncode;
        bmp = bmp->pnext;
        position = 0;
    }
    return 0;
}

int bsDecode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS) {
   return bsXncode(data, size, position, bmpS, bDecode);
}

int bsEncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS) {
    return bsXncode(data, size, position, bmpS, bEncode);
}

int isFileBmp(struct Bmp *bmp) {
    uint8 buf[2];
    if (bmp->size < BMP_HEADER_SIZE)
        return 0;
    bRead(buf, 2, 0, bmp);
    return buf[0] == 'B' && buf[1] == 'M';
}

uint getBmpColorDepth(struct Bmp *bmp) {
    uint8 buf[2];
    bRead(buf, 2, 28, bmp);
    return buf[0] + buf[1] * 256;
}

void fillBmpStruct(struct Bmp *bmp) {
    // dimensions
    bRead((uint8 *) &bmp->width, 4, 18, bmp);
    bRead((uint8 *) &bmp->height, 4, 22, bmp);
    printInfo("width: %d, height: %d\n", bmp->width, bmp->height);

    // row size
    bmp->rowSize = bmp->width * COLORS_PER_PIXEL;
    bmp->padding = (4 - (bmp->rowSize % 4)) % 4;
    bmp->rowSize += bmp->padding;
    printInfo("row size: %d B, row padding: %d B\n", bmp->rowSize, bmp->padding);

    // capacity
    bmp->virtualSize = bmp->width * bmp->height * COLORS_PER_PIXEL * USED_BITS_PER_PIXEL / 8;
    printInfo("virtual size: %lu.%.2lu MiB\n", bmp->virtualSize / 1024 / 1024, (100 * bmp->virtualSize / 1024 / 1024) % 100);

    // header size
    bRead((uint8 *) &bmp->headerSize, 4, 10, bmp);
    printInfo("header size: %d B\n", bmp->headerSize);

    // idx of the file
    bRead((uint8 *) &bmp->idx, 2, BMP_IDX_OFFSET, bmp);
}

int handleFile(void* data, const char *name, int namlen, loff_t offset, u64 ino, uint d_type) {
    struct BmpStorage *bmpS = (struct BmpStorage *) data;
    struct Bmp *bmp;
    char* fullPath;
    uint16 bmpsCountReported;
    int err;

    if (d_type != DT_REG) return 0;

    printInfo("===> %.*s\n", namlen, name);
    bmp = kmalloc(sizeof(struct Bmp), GFP_KERNEL);
    if (bmp == NULL) {
        printError("failed to allocate bmp struct\n");
        return -ENOMEM;
    }
    bmp->pnext = NULL;

    fullPath = kmalloc(strlen(bmpS->backingPath) + 1 + namlen + 1, GFP_KERNEL);
    if (fullPath == NULL) {
        printError("failed to allocate fullPath string\n");
        err = -ENOMEM;
        goto FREE_BMP;
    }
    strcpy(fullPath, bmpS->backingPath);
    strcat(fullPath, "/");
    strncat(fullPath, name, namlen);
    bmp->fd = filp_open(fullPath, O_RDWR, 0644);
    kfree(fullPath);
    if (IS_ERR_OR_NULL(bmp->fd)) {
        printError("failed to open file\n");
        err = PTR_ERR(bmp->fd);
        goto FREE_BMP;
    }
    bmp->size = bmp->fd->f_inode->i_size;
    printInfo("file size: %ld.%.2ld MiB\n", bmp->size / 1024 / 1024, (100 * bmp->size / 1024 / 1024) % 100);

    if (!isFileBmp(bmp)) {
        printInfo("file is not a bmp\n");
        err = -EINVAL;
        goto CLOSE_FILE;
    }

    if (getBmpColorDepth(bmp) != 32) {
        printError("only 32-bit ARGB bitmaps are supported\n");
        err = -EINVAL;
        goto CLOSE_FILE;
    }

    fillBmpStruct(bmp);
    
    bRead((uint8 *) &bmpsCountReported, 2, BMP_COUNT_OFFSET, bmp);
    if (bmpsCountReported == 0) {
        printError("file is not a part of a bmp storage\n");
        if (bmpS->bmps == NULL) {
            printError("you need to initialize this folder with helper program\n");
        }
        err = -EINVAL;
        goto CLOSE_FILE;
    }

    bmp->virtualOffset = bmpS->totalVirtualSize;
    bmpS->totalVirtualSize += bmp->virtualSize;

    // add to list
    if (bmpS->bmps == NULL) {
        bmpS->bmps = bmp;
        bmpS->count = bmpsCountReported;
    } else {
        struct Bmp *bmpPrev = NULL;
        struct Bmp *bmpCurr = bmpS->bmps;

        if (bmpsCountReported != bmpS->count) {
            printError("file count mismatch, different files have reported different count\n");
            printError("this file belongs to other or none bmp storage");
            err = -EINVAL;
            goto CLOSE_FILE;
        }

        // add to the correct place based on idx
        while (bmpCurr != NULL && bmpCurr->idx < bmp->idx) {
            bmpPrev = bmpCurr;
            bmpCurr = bmpCurr->pnext;
        }
        if (bmpPrev == NULL) {
            bmp->pnext = bmpS->bmps;
            bmpS->bmps = bmp;
        } else {
            bmp->pnext = bmpCurr;
            bmpPrev->pnext = bmp;
        }
    }

    return 0;

CLOSE_FILE:
    filp_close(bmp->fd, NULL);
FREE_BMP:
    kfree(bmp);
    return err;
}

int openBmps(struct BmpStorage *bmpS) {
    int err = 0;
    uint idx = 0;
    struct Bmp *bmp;
    
    bmpS->totalVirtualSize = 0;
    if (( err = readdir(bmpS->backingPath, handleFile, (void*)bmpS) )) {
        printError("failed to read directory %s\n", bmpS->backingPath);
        closeBmps(bmpS);
        return err;
    }
    printInfo("===<\n");

    bmp = bmpS->bmps;
    while(bmp != NULL) {
        if (bmp->idx != idx++) {
            printError("failed to open all bmps, %d is missing\n", idx - 1);
            printError("there should be %d bmps in this folder\n", bmpS->count);
            closeBmps(bmpS);
            return -EINVAL;
        }
        bmp = bmp->pnext;
    }
    if (idx != bmpS->count) {
        printError("there should be %d bmps, but only %d were found\n", bmpS->count, idx);
        closeBmps(bmpS);
        return -EINVAL;
    }

    printInfo("total virtual size: %lu.%.2lu MiB (%lu B)\n", bmpS->totalVirtualSize / 1024 / 1024, (100 * bmpS->totalVirtualSize / 1024 / 1024) % 100, bmpS->totalVirtualSize);

    return err;
}

void closeBmps(struct BmpStorage *bmpS) {
    struct Bmp *bmp = bmpS->bmps;
    struct Bmp *nextBmp;
    while (bmp != NULL) {
        if (bmp->fd) filp_close(bmp->fd, NULL);

        nextBmp = bmp->pnext;
        kfree(bmp);
        bmp = nextBmp;
    }
}