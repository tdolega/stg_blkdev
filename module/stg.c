#include "stg.h"

DEFINE_MUTEX(mutex);

void bRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    mutex_lock(&mutex);
    kernel_read(bmp->fd, buffer, size, &position);
    mutex_unlock(&mutex);
}

void bWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    mutex_lock(&mutex);
    kernel_write(bmp->fd, buffer, size, &position);
    mutex_unlock(&mutex);
}

void tRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    // int i;
    // mutex_lock(&mutex);
    // // kernel_read(bmp->fd, buffer, size, &position);
    // mutex_unlock(&mutex);
    // return;
    // if(size > 4) {
    //     printError("size > 4\n");
    //     return;
    // }
    // uint8 confirmBuffer[4];
    // // memcpy(confirmBuffer, bmp->filesim + position, size);
    // memcpy(buffer, bmp->filesim + position, size);
    // // for(i = 0; i < size; i++) {
    // //     if(buffer[i] != confirmBuffer[i]) {
    // //         printError("bad data (%d), (%d)\n", buffer[i], confirmBuffer[i]);
    // //     }
    // // }
}

void tWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    // mutex_lock(&mutex);
    // // kernel_write(bmp->fd, buffer, size, &position);
    // mutex_unlock(&mutex);
    // return;
    // memcpy(bmp->filesim + position, buffer, size);
}

void fRead(void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    // printInfo("1 read %lu B at pos %lld = %d\n", size, position, (int) *buffer);
    // memcpy(buffer, &(bmp->filesim)[position], size);
    memcpy(buffer, bmp->filesim + position, size);
    // char five = 5;
    // memcpy(&buffer, &five, sizeof(char));
    // printInfo("2 read %lu B at pos %lld = %d\n", size, position, (int) *buffer);
}

void fWrite(const void *buffer, ulong size, loff_t position, struct Bmp *bmp) {
    // printInfo("1 write %lu B at pos %lld = %d\n", size, position, buffer);
    memcpy(bmp->filesim + position, buffer, size);
    // memcpy(&(bmp->filesim)[position], buffer, size);
    // printInfo("2 write %lu B at pos %lld = %d\n", size, position, buffer);
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
    // printInfo("virtual size: %.2f MiB\n", (float) bmp->virtualSize / 1024 / 1024);
    fillBmpHeaderSize(bmp);
    printInfo("header size: %d B\n", bmp->headerSize);
}

u8 *test_data = NULL;

uint openBmps(char **filePaths, struct BmpStorage *bmpS) {
    uint i;
    struct Bmp *bmp;

    char **sortedFilePaths = filePaths;
    // char **sortedFilePaths = kmalloc(sizeof(char *) * bmpS->count, GFP_KERNEL);
    // memcpy(sortedFilePaths, filePaths, sizeof(char *) * bmpS->count);
    // qsort(sortedFilePaths, bmpS->count, sizeof(sortedFilePaths[0]), pstrcmp);

    bmpS->totalVirtualSize = 100 * 1024 * 1024;
    test_data = vmalloc(bmpS->totalVirtualSize );
    if(test_data == NULL) {
        printError("test_data == NULL\n");
        return -ENOMEM;
    }
    return 0;

    mutex_init(&mutex);

    bmpS->totalVirtualSize = 0;
    for (i = 0; i < bmpS->count; i++) {
        bmp = &bmpS->bmps[i];
        bmp->path = sortedFilePaths[i];
        printInfo(">>>> %s\n", bmp->path);

        // bmp->file = fopen(bmp->path, "r+b");
        bmp->fd = filp_open(bmp->path, O_RDWR, 0644); // todo: is it r+b?
        if (IS_ERR_OR_NULL(bmp->fd)) {
            printError("failed to open file\n");
            return PTR_ERR(bmp->fd);
        }
        bmp->size = bmp->fd->f_inode->i_size;
        // bmp->size >>= 9;
        printInfo("file size: %ld MiB\n", bmp->size / 1024 / 1024);
        // printInfo("file size: %.2f MiB\n", (float) bmp->size / 1024 / 1024);

        if (!isFileBmp(bmp)) {
            printInfo("file is not a bmp\n");
            return -EINVAL;
        }

        if (getBmpColorDepth(bmp) != 32) {
            printError("only 32-bit ARGB bitmaps are supported\n");
            return -EINVAL;
        }

        fillBmpStruct(bmp);

        bmp->filesim = vmalloc(bmp->virtualSize);
        printInfo("vmalloc %d B\n", bmp->virtualSize);
        if(bmp->filesim == NULL) {
            printError("failed to allocate memory for file simulation\n");
            return -ENOMEM;
        }

        bmp->virtualOffset = bmpS->totalVirtualSize;
        bmpS->totalVirtualSize += bmp->virtualSize;
        printInfo("\n");
    }
    // kfree(sortedFilePaths);
    // printInfo("total virtual size: %.2f MiB (%d B)\n\n", (float) bmpS->totalVirtualSize / 1024 / 1024, bmpS->totalVirtualSize);
    return 0;
}

void closeBmps(struct BmpStorage *bmpS) {
    uint i;
    return; // deleteme
    for (i = 0; i < bmpS->count; i++) {
        struct file *fd = bmpS->bmps[i].fd;
        if (fd) filp_close(fd, NULL);
        if (bmpS->bmps[i].filesim) vfree(bmpS->bmps[i].filesim);
    }
}

///////////////

uint pixelIdxToBmpIdx(struct Bmp *bmp, uint pixel) {
    uint row = pixel / bmp->width;
    uint col = pixel % bmp->width;
    return row * bmp->rowSize + col * COLORS_PER_PIXEL + bmp->headerSize;
}

void bEncode(const uint8 *data, ulong size, loff_t position, struct Bmp *bmp) {
    uint byteIdx;
    uint8 colorIdx;
    for (byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = data[byteIdx];
        uint pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color;
            uint8 twoBits;
            // bRead(&color, 1, pixelIdx + colorIdx, bmp);
            fRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = (byte >> (colorIdx * 2)) & 0b11;
            color = (color & 0b11111100) | twoBits;
            // bWrite(&color, 1, pixelIdx + colorIdx, bmp);
            fWrite(&color, 1, pixelIdx + colorIdx, bmp);
        }
    }
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
            // bRead(&color, 1, pixelIdx + colorIdx, bmp);
            fRead(&color, 1, pixelIdx + colorIdx, bmp);
            twoBits = color & 0b00000011;
            byte |= twoBits << (colorIdx * 2);
        }
        data[byteIdx] = byte;
    }
}

int bsEncode(const void *data, ulong size, loff_t position, struct BmpStorage *bmpS) {
    uint bmpIdx = 0;

    ///
    memcpy(test_data + position, data, size);
    return 0;
    ///
    
    if (position + size > bmpS->totalVirtualSize) {
        printError("not enough space\n");
        return 1;
    }

    while (bmpIdx < bmpS->count - 1 && position >= bmpS->bmps[bmpIdx].virtualSize) {
        position -= bmpS->bmps[bmpIdx].virtualSize;
        bmpIdx++;
    }

    while (size > 0) {
        struct Bmp *bmp = &bmpS->bmps[bmpIdx];
        uint bmpSize = bmp->virtualSize - position;
        uint bytesToWrite = bmpSize < size ? bmpSize : size;
        // bEncode(data, bytesToWrite, position, bmp);
        fWrite(data, bytesToWrite, position, bmp);
        data += bytesToWrite;
        size -= bytesToWrite;
        bmpIdx++;
        position = 0;
    }
    return 0;
}

int bsDecode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS) {
    uint bmpIdx = 0;

    ///
    memcpy(data, test_data + position, size);
    return 0;
    ///

    if (position + size > bmpS->totalVirtualSize) {
        printError("not enough space\n");
        return 1;
    }

    // while (bmpIdx < bmpS->count - 1 && position >= bmpS->bmps[bmpIdx].virtualSize) {
    //     position -= bmpS->bmps[bmpIdx].virtualSize;
    //     bmpIdx++;
    // }

    while (size > 0) {
        struct Bmp *bmp = &bmpS->bmps[bmpIdx];
        uint bmpSize = bmp->virtualSize - position;
        uint bytesToRead = bmpSize < size ? bmpSize : size;
        // bDecode(data, bytesToRead, position, bmp);
        fRead(data, bytesToRead, position, bmp);
        data += bytesToRead;
        size -= bytesToRead;
        bmpIdx++;
        position = 0;
    }
    return 0;
}