#ifndef USERLANDABLE_DEFINITIONS_H
#define USERLANDABLE_DEFINITIONS_H

#include <stdio.h>

#define uint8 unsigned char
#define uint unsigned int

#define COLORS_PER_PIXEL 4
#define BMP_HEADER_SIZE 54

struct Bmp {
    FILE *file;
    long size;
    char* path;

    uint width;
    uint height;

    uint headerSize;
    uint rowSize;
    uint padding;

    uint virtualSize;
    uint virtualOffset;
};

struct BmpStorage {
    struct Bmp *bmps;
    uint count;
    uint totalVirtualSize;
};

/////

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

#endif //USERLANDABLE_DEFINITIONS_H
