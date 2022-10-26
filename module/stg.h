#include "definitions.h"

uint openBmps(char **filePaths, uint nrFiles, struct BmpStorage *bmpS);
void closeBmps(struct BmpStorage *bmpS);

typedef void(*xncoder_t)(uint8 *, ulong, loff_t, struct Bmp *);

int bsEncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
int bsDecode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
