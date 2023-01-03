# stg_blkdev
steganography block device kernel module for Linux 6.1

TODO:
- umount command temporarly broken! fix it to use `stat` for getting device path
- block the user from editing/deleting bitmaps that are currently mounted
- do not display errors if there are also other, unrelated files in the folder with bitmaps
- hide question from mkfs being displayed during formatting - "/dev/stga contains ISO-8859 text, with very long lines..."
