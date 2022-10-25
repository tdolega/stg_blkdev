#include "definitions.h"

uint openBmps(char **filePaths, struct BmpStorage *bmpS);
void closeBmps(struct BmpStorage *bmpS);

int bsEncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
int bsDecode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
