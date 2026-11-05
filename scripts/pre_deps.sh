#!/bin/bash

if [[ "$MSYSTEM" != "MINGW64" ]]; then
    echo "Please run this script in an msys2 mingw64 environment. (blue one)"
    exit 1
fi

# get ze toolchain 
pacman -Sy pkgfile mingw-w64-x86_64-toolchain mingw-w64-x86_64-llvm-tools rsync
pkgfile --update
