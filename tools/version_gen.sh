#!/bin/bash

DIR=$1
[ "$DIR" ] || { echo "Syntax: version_gen.sh directory"; exit 1; }
[ -d "$DIR" ] || { echo "Directory does not exist: $DIR"; exit 1; }
OUTPUT=$DIR/version.cpp
VERSION=$(cat version.txt)
echo "version_gen: $VERSION -> $OUTPUT"
cat > $OUTPUT <<EOF
#include <string>
extern const std::string VERSION = "$VERSION";
EOF