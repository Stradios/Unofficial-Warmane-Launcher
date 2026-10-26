#!/bin/sh
# Cross-compile unofficial-warmane-launcher.exe (Windows x86-64) from Linux with mingw-w64.
set -e
cd "$(dirname "$0")"
CXX=x86_64-w64-mingw32-g++
RC=x86_64-w64-mingw32-windres
mkdir -p build
$RC resources.rc -O coff -o build/resources.o
$CXX -O2 -std=gnu++17 -Wall -municode -mwindows -static -static-libgcc -static-libstdc++ \
  -I../shared -s src/main.cpp build/resources.o -o build/unofficial-warmane-launcher.exe \
  -lgdiplus -lcomctl32 -lcomdlg32 -lshell32 -lole32 -loleaut32 -luuid -lgdi32 -luser32
ls -la build/unofficial-warmane-launcher.exe
