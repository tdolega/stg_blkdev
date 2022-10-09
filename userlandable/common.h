#ifndef USERLANDABLE_COMMON_H
#define USERLANDABLE_COMMON_H

#include <stdlib.h>

#include "definitions.h"
#include "utils.h"
#include "separate.h"

uint openBmps(char **filePaths, struct BmpStorage *bmpS);
void closeBmps(struct BmpStorage *bmpS);

int bsEncode(const uint8 *data, uint size, uint position, struct BmpStorage *bmpS);
int bsDecode(uint8 *data, uint size, uint position, struct BmpStorage *bmpS);

#endif //USERLANDABLE_COMMON_H
