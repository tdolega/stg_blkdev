#include "common.h"

int isFileBmp(struct Bmp *bmp) {
    if (bmp->size < BMP_HEADER_SIZE)
        return 0;
    uint8 buf[2];
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
    printf("width: %d, height: %d\n", bmp->width, bmp->height);
    fillBmpRowSize(bmp);
    printf("row size: %d B, row padding: %d B\n", bmp->rowSize, bmp->padding);
    fillCapacity(bmp);
    printf("virtual size: %.2f MiB\n", (float) bmp->virtualSize / 1024 / 1024);
    fillBmpHeaderSize(bmp);
    printf("header size: %d B\n", bmp->headerSize);
}

uint openBmps(char **filePaths, struct BmpStorage *bmpS) {
    char **sortedFilePaths = malloc(sizeof(char *) * bmpS->count);
    memcpy(sortedFilePaths, filePaths, sizeof(char *) * bmpS->count);
    qsort(sortedFilePaths, bmpS->count, sizeof(sortedFilePaths[0]), pstrcmp);

    bmpS->totalVirtualSize = 0;
    for (uint i = 0; i < bmpS->count; i++) {
        struct Bmp *bmp = &bmpS->bmps[i];
        bmp->path = sortedFilePaths[i];
        printf(">>>> %s\n", bmp->path);

        bmp->file = fopen(bmp->path, "r+b");
        if (bmp->file == NULL) {
            printf("ERROR: failed to open file\n");
            return 1;
        }
        fseek(bmp->file, 0, SEEK_END);
        bmp->size = ftell(bmp->file);
        fseek(bmp->file, 0, SEEK_SET);
        printf("file size: %.2f MiB\n", (float) bmp->size / 1024 / 1024);

        if (!isFileBmp(bmp)) {
            printf("file is not a bmp\n");
            return 1;
        }

        if (getBmpColorDepth(bmp) != 32) {
            printf("ERROR: only 32-bit ARGB bitmaps are supported\n");
            return 1;
        }

        fillBmpStruct(bmp);
        bmp->virtualOffset = bmpS->totalVirtualSize;
        bmpS->totalVirtualSize += bmp->virtualSize;
        printf("\n");
    }
    free(sortedFilePaths);
    printf("total virtual size: %.2f MiB (%d B)\n\n", (float) bmpS->totalVirtualSize / 1024 / 1024, bmpS->totalVirtualSize);
    return 0;
}

void closeBmps(struct BmpStorage *bmpS) {
    for (uint i = 0; i < bmpS->count; i++) {
        FILE *file = bmpS->bmps[i].file;
        if (file)fclose(file);
    }
    free(bmpS->bmps);
    free(bmpS);
}

///////////////

uint pixelIdxToBmpIdx(struct Bmp *bmp, uint pixel) {
    uint row = pixel / bmp->width;
    uint col = pixel % bmp->width;
    return row * bmp->rowSize + col * COLORS_PER_PIXEL + bmp->headerSize;
}

void bEncode(const uint8 *data, uint size, uint position, struct Bmp *bmp) {
    for (uint byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = data[byteIdx];
        uint pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (uint8 colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            uint8 twoBits = (byte >> (colorIdx * 2)) & 0b11;
            color = (color & 0b11111100) | twoBits;
            bWrite(&color, 1, pixelIdx + colorIdx, bmp);
        }
    }
}

void bDecode(uint8 *data, uint size, uint position, struct Bmp *bmp) {
    for (int byteIdx = 0; byteIdx < size; byteIdx++) {
        uint8 byte = 0;
        uint pixelIdx = pixelIdxToBmpIdx(bmp, position + byteIdx);
        for (uint8 colorIdx = 0; colorIdx < COLORS_PER_PIXEL; colorIdx++) {
            uint8 color;
            bRead(&color, 1, pixelIdx + colorIdx, bmp);
            uint8 twoBits = color & 0b00000011;
            byte |= twoBits << (colorIdx * 2);
        }
        data[byteIdx] = byte;
    }
}

int bsEncode(const uint8 *data, uint size, uint position, struct BmpStorage *bmpS) {
    if (position + size > bmpS->totalVirtualSize) {
        printf("ERROR: not enough space\n");
        return 1;
    }

    uint bmpIdx = 0;
    uint bmpPosition = position;
    while (bmpIdx < bmpS->count - 1 && bmpPosition >= bmpS->bmps[bmpIdx].virtualSize) {
        bmpPosition -= bmpS->bmps[bmpIdx].virtualSize;
        bmpIdx++;
    }

    while (size > 0) {
        struct Bmp *bmp = &bmpS->bmps[bmpIdx];
        uint bmpSize = bmp->virtualSize - bmpPosition;
        uint bytesToWrite = bmpSize < size ? bmpSize : size;
        bEncode(data, bytesToWrite, bmpPosition, bmp);
        data += bytesToWrite;
        size -= bytesToWrite;
        bmpIdx++;
        bmpPosition = 0;
    }
    return 0;
}

int bsDecode(uint8 *data, uint size, uint position, struct BmpStorage *bmpS) {
    if (position + size > bmpS->totalVirtualSize) {
        printf("ERROR: not enough space\n");
        return 1;
    }

    uint bmpIdx = 0;
    uint bmpPosition = position;
    while (bmpIdx < bmpS->count - 1 && bmpPosition >= bmpS->bmps[bmpIdx].virtualSize) {
        bmpPosition -= bmpS->bmps[bmpIdx].virtualSize;
        bmpIdx++;
    }

    while (size > 0) {
        struct Bmp *bmp = &bmpS->bmps[bmpIdx];
        uint bmpSize = bmp->virtualSize - bmpPosition;
        uint bytesToRead = bmpSize < size ? bmpSize : size;
        bDecode(data, bytesToRead, bmpPosition, bmp);
        data += bytesToRead;
        size -= bytesToRead;
        bmpIdx++;
        bmpPosition = 0;
    }
    return 0;
}