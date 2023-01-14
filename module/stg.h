#include "definitions.h"
#include "diriter.h"

int openBmps(struct BmpStorage *bmpS);
void closeBmps(struct BmpStorage *bmpS);

typedef void(*xxcoder_t)(uint8 *, ulong, loff_t, struct Bmp *);

int bsEncode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
int bsDecode(void *data, ulong size, loff_t position, struct BmpStorage *bmpS);
