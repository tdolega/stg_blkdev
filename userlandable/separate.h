#ifndef USERLANDABLE_SEPARATE_H
#define USERLANDABLE_SEPARATE_H

#include <stdio.h>

#include "definitions.h"

void bRead(uint8 *buffer, uint size, uint position, struct Bmp *bmp);

void bWrite(uint8 *buffer, uint size, uint position, struct Bmp *bmp);

#endif //USERLANDABLE_SEPARATE_H
