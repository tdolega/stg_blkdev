#include "main.h"

void printHelp() {
    printf("Usage: stg_helper [mode] [sourceFolder]\n");
    printf("    [modes]:\n");
    printf("        init   - initializes bitmaps from [sourceFolder] with special header to use them as disk\n");
    printf("            stg_helper init ~/myBmps \n");
    printf("        clean  - removes special header from files in [sourceFolder]\n");
    printf("            stg_helper clean ~/myBmps \n");
    printf("        mount  - mount a [sourceFolder]\n");
    printf("            stg_helper mount ~/myBmps \n");
    printf("        umount - unmount a [sourceFolder]\n");
    printf("            stg_helper umount ~/myBmps \n");
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
    uint16 zero = 0;
    while (openBmp != NULL) {
        fseek(openBmp->file, BMP_IDX_OFFSET, SEEK_SET);
        fwrite(&zero, 1, 2, openBmp->file);
        fwrite(&zero, 1, 2, openBmp->file);
        openBmp = openBmp->pnext;
    }
    printf("cleaned %d bitmap files\n", bmpsCount);
    closeBmps(openedBmps);
    return 0;
}

int mount(char *folder) {
    int err;
    if(system("modinfo stg_blkdev"REDIRECT_STDOUT)) {
        printf("ERROR: stg_blkdev is already mounted\n");
        return 1;
    }

    char* modprobeCmd = "modprobe stg_blkdev backingPath=";
    char* modprobeCmdFull = malloc(strlen(modprobeCmd) + strlen(folder) + 1);
    sprintf(modprobeCmdFull, "%s%s", modprobeCmd, folder);
    err = system(modprobeCmdFull);
    free(modprobeCmdFull);
    if(err) {
        printf("ERROR: failed to load stg_blkdev module\n");
        return err;
    }

    int isFormatted = system("lsblk /dev/sbd -o FSTYPE -n | grep -q 'ext4'");
    if(!isFormatted) {
        err = system("mkfs.ext4 /dev/sbd -f -L stg");
        if(err) {
            printf("ERROR: failed to mkfs.ext4\n");
            return err;
        }
    }

    err = system("mkdir -p /mnt/stg");
    if(err) {
        printf("ERROR: failed to mkdir /mnt/stg\n");
        return err;
    }
    err = system("mount /dev/sbd /mnt/stg");
    if(err) {
        printf("ERROR: failed to mount /dev/sbd\n");
        return err;
    }
    err = system("chown $USER /mnt/stg");
    if(err) {
        printf("ERROR: failed to chown /mnt/stg\n");
        return err;
    }
    return 0;
}

int umount(char *folder) {
    int err;
    err = system("umount /dev/sbd");
    if(err) {
        printf("ERROR: failed to umount /dev/sbd\n");
        return err;
    }
    err = system("modprobe stg_blkdev -r");
    if(err) {
        printf("ERROR: failed to unload stg_blkdev module -r\n");
        return err;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        printHelp();
        exit(0);
    }

    char *mode = argv[1];
    char *folder = argv[2];

    if(strcmp(mode, "init") == 0) {
        return init(folder);
    } else if(strcmp(mode, "clean") == 0) {
        return clean(folder);
    } else if(strcmp(mode, "mount") == 0) {
        return mount(folder);
    } else if(strcmp(mode, "umount") == 0) {
        return umount(folder);
    } else {
        printf("ERROR: unknown mode %s\n", mode);
        printHelp();
    }
    return 0;
}