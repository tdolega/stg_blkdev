#!/bin/bash

if [ $# -lt 2 ]; then
    echo "convert any image to 32-bit ARGB BMP with help of 'convert' command"
    echo "example: ./convertImgToBmp inputFile.jpg outputFile.bmp"
    exit 1
fi

convert $1 -alpha set -define bmp:format=bmp4 $2
