#include "stg.h"

void bRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    kernel_read(bmp->fd, buffer, size, &position);
}

void bWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    kernel_write(bmp->fd, buffer, size, &position);
}

// void fRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
//     memcpy(buffer, bmp->filesim + position, size);
// }

// void fWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
//     memcpy(bmp->filesim + position, buffer, size);
// }

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
            byte |= twoBits << (colorIdx * 2);
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
            twoBits = (byte >> (colorIdx * 2)) & 0b11;
            color = (color & 0b11111100) | twoBits;
            bWrite(&color, 1, pixelIdx + colorIdx, bmp);
        }
    }
}

int bsXncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS, xncoder_t xncoder) {
    struct Bmp *bmp = bmpS->bmps;
    
    if (position + size > bmpS->totalVirtualSize) {
        printError("not enough space\n");
        return -ENOSPC; // instead of erroring out, truncating size is also a possibility...
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
    bmp->virtualSize = bmp->width * bmp->height * COLORS_PER_PIXEL * 2 / 8;
    printInfo("virtual size: %lu.%.2lu MiB\n", bmp->virtualSize / 1024 / 1024, (100 * bmp->virtualSize / 1024 / 1024) % 100);

    // header size
    bRead((uint8 *) &bmp->headerSize, 4, 10, bmp);
    printInfo("header size: %d B\n", bmp->headerSize);
}

uint openBmps(char **filePaths, uint nrFiles, struct BmpStorage *bmpS) {
    bmpS->totalVirtualSize = 0;
    for (uint i = 0; i < nrFiles; i++) {
        struct Bmp *bmp = kmalloc(sizeof(struct Bmp), GFP_KERNEL);
        bmp->pnext = NULL;
        bmp->path = filePaths[i];
        printInfo("===> %s\n", bmp->path);

        bmp->fd = filp_open(bmp->path, O_RDWR, 0644);
        if (IS_ERR_OR_NULL(bmp->fd)) {
            printError("failed to open file\n");
            return PTR_ERR(bmp->fd);
        }
        bmp->size = bmp->fd->f_inode->i_size;
        printInfo("file size: %ld.%.2ld MiB\n", bmp->size / 1024 / 1024, (100 * bmp->size / 1024 / 1024) % 100);

        if (!isFileBmp(bmp)) {
            printInfo("file is not a bmp\n");
            return -EINVAL;
        }

        if (getBmpColorDepth(bmp) != 32) {
            printError("only 32-bit ARGB bitmaps are supported\n");
            return -EINVAL;
        }

        fillBmpStruct(bmp);

        // bmp->filesim = vmalloc(bmp->size);
        // printInfo("vmalloc %lu B\n", bmp->virtualSize);
        // if(bmp->filesim == NULL) {
        //     printError("failed to allocate memory for file simulation\n");
        //     return -ENOMEM;
        // }

        bmp->virtualOffset = bmpS->totalVirtualSize;
        bmpS->totalVirtualSize += bmp->virtualSize;

        // add to list
        if (bmpS->bmps == NULL) {
            bmpS->bmps = bmp;
        } else {
            struct Bmp *last = bmpS->bmps;
            while (last->pnext != NULL)
                last = last->pnext;
            last->pnext = bmp;
        }
    }
    printInfo("===<\n");
    printInfo("total virtual size: %lu.%.2lu MiB (%lu B)\n", bmpS->totalVirtualSize / 1024 / 1024, (100 * bmpS->totalVirtualSize / 1024 / 1024) % 100, bmpS->totalVirtualSize);
    return 0;
}

void closeBmps(struct BmpStorage *bmpS) {
    struct Bmp *bmp = bmpS->bmps;
    struct Bmp *nextBmp;
    while (bmp != NULL) {
        if (bmp->fd) filp_close(bmp->fd, NULL);
        else printError("file descriptor is NULL\n");
        // vfree(bmp->filesim);

        nextBmp = bmp->pnext;
        kfree(bmp);
        bmp = nextBmp;
    }
}