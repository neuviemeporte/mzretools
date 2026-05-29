#!/bin/bash
set -euo pipefail

if [ ! -f googletest/CMakeLists.txt ]; then
    git submodule init && git submodule update  
fi

if [ ! -f tools/emulators/kvikdos/Makefile ]; then
    git submodule update --init tools/emulators/kvikdos
fi

(cd tools/emulators/kvikdos && make)

[ -d debug ] || mkdir debug 
cd debug && cmake .. && make -j
