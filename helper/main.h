#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include "common.h"

#define IOCTL_DEV_ADD 55001
#define IOCTL_DEV_REMOVE 55002
#define MAX_BACKING_LEN 1024
