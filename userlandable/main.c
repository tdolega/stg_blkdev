#include "main.h"

#define TESTING_SIZE (73219)

int run(struct BmpStorage *bmpS) {
    uint8 randomData[TESTING_SIZE];
    uint8 readBuffer[TESTING_SIZE];
    long long t0, t1;
    t0 = currentTimestamp();
    for (uint i = 0; i < TESTING_SIZE; i++) {
        randomData[i] = (uint8) rand();
    }

//    if (bsEncode(randomData, TESTING_SIZE, 0, bmpS)) return 1;
    if (bsEncode(randomData, 10000, 0, bmpS)) return 1;
    if (bsEncode(randomData+1000, 5000, 1000, bmpS)) return 1;
    if (bsEncode(randomData+10000, 60000, 10000, bmpS)) return 1;
    if (bsEncode(randomData+70000, 73219-70000, 70000, bmpS)) return 1;

//    if (bsDecode(readBuffer, TESTING_SIZE, 0, bmpS)) return 1;
    if (bsDecode(readBuffer, 60000, 0, bmpS)) return 1;
    if (bsDecode(readBuffer+73219-20000, 20000, 73219-20000, bmpS)) return 1;

    int bad = 0;
    for (uint i = 0; i < TESTING_SIZE; i++)
        if (randomData[i] != readBuffer[i]) {
            bad++;
            if (bad < 10)
                printf("error at %d: "BYTE_TO_BINARY_PATTERN" != "BYTE_TO_BINARY_PATTERN"\n", i, BYTE_TO_BINARY(randomData[i]), BYTE_TO_BINARY(readBuffer[i]));
            else
                printf(".");
        }
    if (bad) printf("\nBad: %d\n", bad);
    else printf("OK\n");
    t1 = currentTimestamp();

    printf("Time: %lld ms\n", t1 - t0);

    return bad;
}

int main() {
    char *filePaths[] = {"/tmp/bmps/1.bmp", "/tmp/bmps/3.bmp", "/tmp/bmps/2.bmp"};
    struct BmpStorage *bmpS = malloc(sizeof(struct BmpStorage));
    bmpS->count = sizeof(filePaths) / sizeof(char *);
    bmpS->bmps = malloc(sizeof(struct Bmp) * bmpS->count);

    if (openBmps(filePaths, bmpS))
        goto ERROR;

    if (run(bmpS)) goto ERROR;

    closeBmps(bmpS);
    printf("\n:)\n");
    return 0;

    ERROR:
    closeBmps(bmpS);
    printf("\n:(\n");
    return 1;
}