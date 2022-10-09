#include "utils.h"

int pstrcmp(const void *a, const void *b) {
    return strcmp(*(const char **) a, *(const char **) b);
}

long long currentTimestamp() {
    struct timeval te;
    gettimeofday(&te, NULL);
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
    return milliseconds;
}