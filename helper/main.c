#include "main.h"

int printHelp() {
    printf("Usage: stg_helper [mode] [sourceFolder?] [mountpoint?]\n");
    printf("    typical modes:\n");
    printf("        init - initializes bitmaps from [sourceFolder] with special header to use them as disk\n");
    printf("            stg_helper init ~/myBmps\n");
    printf("        clean - removes special header from files in [sourceFolder]\n");
    printf("            stg_helper clean ~/myBmps\n");
    printf("        mount - add and mount a [sourceFolder] to [mountpoint]\n");
    printf("            stg_helper mount ~/myBmps /mnt/stg\n");
    printf("        umount - unmount and remove a disk based on [sourceFolder]\n");
    printf("            stg_helper umount ~/myBmps\n");
    printf("    advanced modes:\n");
    printf("        add - add a disk based on [sourceFolder]\n");
    printf("            stg_helper add ~/myBmps\n");
    printf("        remove - remove a disk based on [sourceFolder]\n");
    printf("            stg_helper remove ~/myBmps\n");
    printf("        load - load driver\n");
    printf("            stg_helper load\n");
    printf("        unload - unload driver\n");
    printf("            stg_helper unload\n");
    return 1;
}

int isBmpFile(FILE *file, uint fileSize) {
    uint8 buf[2];
    if (fileSize < BMP_HEADER_SIZE)
        return 0;
    fseek(file, 0, SEEK_SET);
    fread(buf, 1, 2, file);
    return buf[0] == 'B' && buf[1] == 'M';
}

struct OpenBmp {
    FILE *file;
    uint16 idx;
    struct OpenBmp *pnext;
};

uint16 openBmps(char *folder, struct OpenBmp **openBmpsRef) {
    DIR *dir = opendir(folder);
    if (dir == NULL) {
        printf("ERROR: failed to open folder\n");
        return 0;
    }

    uint16 bmpCount = 0;
    struct OpenBmp *openFilesTail = NULL;
    struct dirent *entry;
    while (( entry = readdir(dir) ) != NULL) {
        if(entry->d_type != DT_REG)
            continue;
        char *filenameFull = malloc(strlen(folder) + 1 + strlen(entry->d_name) + 1);
        sprintf(filenameFull, "%s/%s", folder, entry->d_name);
        FILE *file = fopen(filenameFull, "r+");
        free(filenameFull);
        if (file == NULL) {
            printf("ERROR: failed to open file %s\n", entry->d_name);
            continue;
        }

        fseek(file, 0, SEEK_END);
        uint fileSize = ftell(file);
        if (!isBmpFile(file, fileSize)) {
            fclose(file);
            printf("%s is not a bitmap file\n", entry->d_name);
            continue;
        }
        if(*openBmpsRef == NULL) {
            *openBmpsRef = malloc(sizeof(struct OpenBmp));
            openFilesTail = *openBmpsRef;
        } else {
            openFilesTail->pnext = malloc(sizeof(struct OpenBmp));
            openFilesTail = openFilesTail->pnext;
        }
        openFilesTail->file = file;
        openFilesTail->idx = bmpCount++;
        if(bmpCount == 0) {
            printf("ERROR: too many files\n");
            break;
        }
    }
    openFilesTail->pnext = NULL;
    closedir(dir);
    return bmpCount;
}

void closeBmps(struct OpenBmp *openedBmps) {
    while(openedBmps != NULL) {
        struct OpenBmp *openedBmp = openedBmps;
        fclose(openedBmp->file);
        openedBmps = openedBmp->pnext;
        free(openedBmp);
    }
}

int init(char *folder) {
    struct OpenBmp *openedBmps = NULL;
    uint16 bmpsCount = openBmps(folder, &openedBmps);
    struct OpenBmp *openBmp = openedBmps;
    while (openBmp != NULL) {
        fseek(openBmp->file, BMP_IDX_OFFSET, SEEK_SET);
        fwrite(&openBmp->idx, 1, 2, openBmp->file);
        fwrite(&bmpsCount, 1, 2, openBmp->file);
        openBmp = openBmp->pnext;
    }
    printf("initialized %d bitmap files\n", bmpsCount);
    if(bmpsCount == 0 && openedBmps != NULL) {
        return 1; // bmpCount overflowed
    }
    closeBmps(openedBmps);
    return 0;
}

int clean(char *folder) {
    struct OpenBmp *openedBmps = NULL;
    uint16 bmpsCount = openBmps(folder, &openedBmps);
    struct OpenBmp *openBmp = openedBmps;
    uint zero = 0;
    while (openBmp != NULL) {
        fseek(openBmp->file, BMP_IDX_OFFSET, SEEK_SET);
        fwrite(&zero, 1, 4, openBmp->file);
        openBmp = openBmp->pnext;
    }
    printf("cleaned %d bitmap files\n", bmpsCount);
    closeBmps(openedBmps);
    return 0;
}

int isCtlLoaded() {
    return system("modinfo stg_blkdev"REDIRECT_STDOUT);
}

int loadCtl() {
    return system("modprobe stg_blkdev");
}

int unloadCtl() {
    return system("modprobe -r stg_blkdev");
}

int sendIoCtl(int command, char *folder, char **name) {
    int err = 0;
    int fd = open("/dev/stg_manager", O_RDWR);
    if(fd < 0) {
        printf("ERROR: failed to open /dev/stg_manager\n");
        return fd;
    }

    char *buf = malloc(MAX_BACKING_LEN);
    strncpy(buf, folder, MAX_BACKING_LEN - 1);
    buf[MAX_BACKING_LEN - 1] = 0;

    if(strnlen(buf, MAX_BACKING_LEN) >= MAX_BACKING_LEN - 1) {
        printf("ERROR: path too long\n");
        err = 1;
    } else {
        err = ioctl(fd, command, buf);
        if(err == 0 && name != NULL) {
            char* name1 = "/dev/";
            *name = malloc(strlen(name1) + strlen(buf) + 1);
            sprintf(*name, "%s%s", name1, buf);
        }
    }

    free(buf);
    close(fd);

    return err;
}

int addDisk(char *folder, char **dev) {
    return sendIoCtl(IOCTL_DEV_ADD, folder, dev);
}

int removeDisk(char *folder) {
    return sendIoCtl(IOCTL_DEV_REMOVE, folder, NULL);
}

int autoMount(char *folder, char* mountpoint) {
    int err = 0;
    char* name = NULL;
    if(!isCtlLoaded()) {
        err = loadCtl();
        if(err) {
            printf("ERROR: failed to load module\n");
            return err;
        }
    }
    err = addDisk(folder, &name);
    if(err) {
        printf("ERROR: failed to add disk\n");
        return err;
    }
    if(name == NULL) {
        printf("ERROR: failed to get device name\n");
        err = 1;
        goto failedToGetName;
    }
    printf("created %s\n", name);
    char *isFormatted1 = "blkid -o value -s TYPE ";
    char *isFormatted3 = " | grep -q ext4";
    char *isFormattedCmd = malloc(strlen(isFormatted1) + strlen(name) + strlen(isFormatted3) + 1);
    sprintf(isFormattedCmd, "%s%s%s", isFormatted1, name, isFormatted3);
    int isFormatted = system(isFormattedCmd) == 0;
    free(isFormattedCmd);
    if(!isFormatted) {
        printf("formatting with ext4\n");
        char *format1 = "mkfs.ext4 -q -m 0 ";
        char *formatCmd = malloc(strlen(format1) + strlen(name) + 1);
        sprintf(formatCmd, "%s%s", format1, name);
        err = system(formatCmd);
        free(formatCmd);
        if(err) {
            printf("ERROR: failed to mkfs\n");
            goto failedToFormat;
        }
    }

    char *mkdir1 = "mkdir -p ";
    char *mkdirCmd = malloc(strlen(mkdir1) + strlen(mountpoint) + 1);
    sprintf(mkdirCmd, "%s%s", mkdir1, mountpoint);
    err = system(mkdirCmd);
    free(mkdirCmd);
    if(err) {
        printf("ERROR: failed to mkdir\n");
        goto failedToMkdir;
    }
    char* mount1 = "mount -t ext4 -o sync";
    char* mountCmd = malloc(strlen(mount1) + 1 + strlen(name) + 1 + strlen(mountpoint) + 1);
    sprintf(mountCmd, "%s %s %s", mount1, name, mountpoint);
    err = system(mountCmd);
    free(mountCmd);
    if(err) {
        printf("ERROR: failed to mount\n");
        goto failedToMount;
    }
    char* chown1 = "if [[ -v SUDO_USER ]]; then chown $SUDO_USER ";
    char* chown3 = "; else chown $USER";
    char* chown5 = "; fi";
    char* chownCmd = malloc(strlen(chown1) + strlen(mountpoint) + strlen(chown3) + strlen(mountpoint) + strlen(chown5) + 1);
    sprintf(chownCmd, "%s%s%s%s%s", chown1, mountpoint, chown3, mountpoint, chown5);
    err = system(chownCmd);
    free(chownCmd);
    if(err) {
        printf("ERROR: failed to chown\n");
        goto failedToChown;
    }

    if(name != NULL) free(name);
    printf("OK\n");
    return 0;

failedToGetName:
failedToFormat:
failedToMkdir:
failedToMount:
failedToChown:
    if(name != NULL) free(name);
    if(removeDisk(folder) == 0){ // may fail but that's ok
        printf("removed disk due to encountered error\n");
    }
    return err;
}
int autoUmount(char *folder) { // todo: is broken, cannot umount by backingPath
    int err = 0;
    char* umountCmd = "umount ";
    char* umountFull = malloc(strlen(umountCmd) + strlen(folder) + 1);
    sprintf(umountFull, "%s %s", umountCmd, folder);
    err = system(umountFull);
    free(umountFull);
    if(err) {
        printf("ERROR: failed to umount\n");
        return err;
    }
    err = removeDisk(folder);
    if(err) {
        return err;
    } else {
        printf("OK\n");
        return 0;
    }
}

int main(int argc, char *argv[]) {
    if(argc < 2) return printHelp();
    int nParams = argc - 2;

    char *mode = argv[1];
    char *folder = argv[2];
    char *mountpoint = argv[3];

    if(strcmp(mode, "init") == 0) {
        if(nParams != 1) return printHelp();
        return init(folder);
    } else if(strcmp(mode, "clean") == 0) {
        if(nParams != 1) return printHelp();
        return clean(folder);
    } else if(strcmp(mode, "mount") == 0) {
        if(nParams != 2) return printHelp();
        return autoMount(folder, mountpoint);
    } else if(strcmp(mode, "umount") == 0) {
        if(nParams != 1) return printHelp();
        return autoUmount(folder);
    } else if(strcmp(mode, "add") == 0) {
        if(nParams != 1) return printHelp();
        char* dev;
        int ret = addDisk(folder, &dev);
        printf("/dev/%s\n", dev);
        free(dev);
        return ret;
    } else if(strcmp(mode, "remove") == 0) {
        if(nParams != 1) return printHelp();
        return removeDisk(folder);
    } else if(strcmp(mode, "load") == 0) {
        if(nParams != 0) return printHelp();
        return loadCtl();
    } else if(strcmp(mode, "unload") == 0) {
        if(nParams != 0) return printHelp();
        return unloadCtl();
    }
    printf("ERROR: unknown mode\n");
    printHelp();
    return 0;
}