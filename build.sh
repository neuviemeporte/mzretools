#!/bin/bash
if [ ! -f googletest/CMakeLists.txt ]; then
    git submodule init && git submodule update  
fi
[ -d debug ] || mkdir debug 
cd debug && cmake .. && make -j