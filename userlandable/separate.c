#include "separate.h"



void bRead(uint8 *buffer, uint size, uint position, struct Bmp *bmp) {
    fseek(bmp->file, position, SEEK_SET);
    fread(buffer, 1, size, bmp->file);
}

void bWrite(uint8 *buffer, uint size, uint position, struct Bmp *bmp) {
    fseek(bmp->file, position, SEEK_SET);
    fwrite(buffer, 1, size, bmp->file);
}