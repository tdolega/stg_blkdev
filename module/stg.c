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

uint pixelIdxToBmpIdx(struct Bmp *bmp, uint pixel) {
    uint row = pixel / bmp->width;
    uint col = pixel % bmp->width;
    return row * bmp->rowSize + col * COLORS_PER_PIXEL + bmp->headerSize;
}

void bDecode(uint8 *data, ulong size, loff_t position, struct Bmp *bmp) {
    uint byteIdx;
    uint8 colorIdx;
    for (byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = 0;
        uint pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color;
            uint8 twoBits;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = color & 0b00000011;
            byte |= twoBits << (colorIdx * 2);
        }
        data[byteIdx] = byte;
    }
}

void bEncode(uint8 *data, ulong size, loff_t position, struct Bmp *bmp) {
    uint byteIdx;
    uint8 colorIdx;
    for (byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = data[byteIdx];
        uint pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color;
            uint8 twoBits;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = (byte >> (colorIdx * 2)) & 0b11;
            color = (color & 0b11111100) | twoBits;
            bWrite(&color, 1, pixelIdx + colorIdx, bmp);
        }
    }
}

int bsXncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS, void(*xncoder)(uint8 *, ulong, loff_t, struct Bmp *)) {
    uint bmpIdx = 0;
    
    if (position + size > bmpS->totalVirtualSize) {
        printError("not enough space\n");
        return -ENOSPC;
    }

    while (bmpIdx < bmpS->count - 1 && position >= bmpS->bmps[bmpIdx].virtualSize) {
        position -= bmpS->bmps[bmpIdx].virtualSize;
        bmpIdx++;
    }

    while (size > 0) {
        struct Bmp *bmp = &bmpS->bmps[bmpIdx];
        ulong posToEnd = bmp->virtualSize - position;
        ulong bytesToXncode = posToEnd < size ? posToEnd : size;

        xncoder(data, bytesToXncode, position, bmp);

        data += bytesToXncode;
        size -= bytesToXncode;
        bmpIdx++;
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

void fillBmpDimensions(struct Bmp *bmp) {
    bRead((uint8 *) &bmp->width, 4, 18, bmp);
    bRead((uint8 *) &bmp->height, 4, 22, bmp);
}

void fillBmpRowSize(struct Bmp *bmp) {
    bmp->rowSize = bmp->width * COLORS_PER_PIXEL;
    bmp->padding = (4 - (bmp->rowSize % 4)) % 4;
    bmp->rowSize += bmp->padding;
}

void fillCapacity(struct Bmp *bmp) {
    bmp->virtualSize = bmp->width * bmp->height * COLORS_PER_PIXEL * 2 / 8;
}

void fillBmpHeaderSize(struct Bmp *bmp) {
    bRead((uint8 *) &bmp->headerSize, 4, 10, bmp);
}

void fillBmpStruct(struct Bmp *bmp) {
    fillBmpDimensions(bmp);
    printInfo("width: %d, height: %d\n", bmp->width, bmp->height);
    fillBmpRowSize(bmp);
    printInfo("row size: %d B, row padding: %d B\n", bmp->rowSize, bmp->padding);
    fillCapacity(bmp);
    printInfo("virtual size: %lu.%.2lu MiB\n", bmp->virtualSize / 1024 / 1024, (100 * bmp->virtualSize / 1024 / 1024) % 100);
    fillBmpHeaderSize(bmp);
    printInfo("header size: %d B\n", bmp->headerSize);
}

uint openBmps(char **filePaths, struct BmpStorage *bmpS) {
    uint i;
    struct Bmp *bmp = bmpS->bmps;

    bmpS->totalVirtualSize = 0;
    for (i = 0; i < bmpS->count; i++, bmp++) {
        bmp->path = filePaths[i];
        printInfo(">>>> %s\n", bmp->path);

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
        printInfo("\n");
    }
    printInfo("total virtual size: %lu.%.2lu MiB (%lu B)\n", bmpS->totalVirtualSize / 1024 / 1024, (100 * bmpS->totalVirtualSize / 1024 / 1024) % 100, bmpS->totalVirtualSize);
    return 0;
}

void closeBmps(struct BmpStorage *bmpS) {
    uint i;
    struct Bmp *bmp = bmpS->bmps;
    for (i = 0; i < bmpS->count; i++, bmp++) {
        if (bmp->fd) filp_close(bmp->fd, NULL);
        else printError("file descriptor is NULL\n");
        // vfree(bmp->filesim);
    }
}