#include <linux/fs.h>

typedef int (*readdir_t)(void *, const char *, int, loff_t, u64, uint);

struct callback_context {
    struct dir_context ctx;
    readdir_t filler;
    void* context;
};

int readdir(const char* path, readdir_t filler, void* context);
