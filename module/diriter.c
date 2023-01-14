#include "diriter.h"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)

bool iterate_dir_callback(struct dir_context *ctx, const char *name, int namlen, loff_t offset, u64 ino, uint d_type) {
    struct callback_context *buf = container_of(ctx, struct callback_context, ctx);
    return !buf->filler(buf->context, name, namlen, offset, ino, d_type);
}

#else

int iterate_dir_callback(struct dir_context *ctx, const char *name, int namlen, loff_t offset, u64 ino, uint d_type) {
    struct callback_context *buf = container_of(ctx, struct callback_context, ctx);
    return buf->filler(buf->context, name, namlen, offset, ino, d_type);
}

#endif

int readdir(const char* path, readdir_t filler, void* context) {
    int err;
    struct callback_context buf = {
        .ctx.actor = (filldir_t) iterate_dir_callback,
        .context = context,
        .filler = filler
    };

    struct file* dir = filp_open(path, O_DIRECTORY, S_IRWXU | S_IRWXG | S_IRWXO);
    if(IS_ERR(dir)) return PTR_ERR(dir);

    err = iterate_dir(dir, &buf.ctx);
    filp_close(dir, NULL);
    return err;
}
