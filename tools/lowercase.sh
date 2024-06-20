#!/bin/bash

temp='xxx123'

[ -f "$temp" ] && { echo "Error: $temp exists!"; exit 1; }

for f in *; do
    dest=$(echo $f | tr A-Z a-z)
    echo "$f -> $dest"
    mv "$f" "$temp" || { echo "Error moving $f to $temp!"; exit 1; }
    mv "$temp" "$dest" || { echo "Error moving $temp to $dest!"; exit 1; }
done
