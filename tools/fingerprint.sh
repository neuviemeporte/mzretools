#!/bin/bash

dir=$1
#debug=1
[ -z "$dir" ] && { echo "Syntax $(basename $0) game_dir"; exit 1; }
[ -d "$dir" ] || { echo "No such directory: $dir"; exit 1; }

function debug() {
	[ "$debug" ] && echo "$@"
}

# check if array contains element
contains() {
	local e match="$1"
	shift
	for e; do [[ "$e" == "$match" ]] && return 0; done
	return 1
}

# go to game directory
debug "entering $dir"
pushd "$dir" &> /dev/null
debug "in dir"

# array of known game files
files=(
'1.pic'
'15flt.3d3'
'2.pic'
'256left.pic'
'256pit.pic'
'256rear.pic'
'256right.pic'
'3.pic'
'4.pic'
'adv.pic'
'armpiece.pic'
'asound.exe'
'ce.3d3'
'ce.3dg'
'ce.3dt'
'ce.wld'
'ceurope.spr'
'cgraphic.exe'
'cockpit.pic'
'dbicons.spr'
'death.pic'
'demo.exe'
'desk.pic'
'ds.exe'
'egame.exe'
'egraphic.exe'
'end.exe'
'f15.com'
'f15.spr'
'f15dgtl.bin'
'f15loadr'
'f15storm.exe'
'gulf.wld'
'hallfame'
'hiscore.pic'
'install.exe'
'isound.exe'
'jp.3d3'
'jp.3dg'
'jp.3dt'
'jp.spr'
'jp.wld'
'labs.pic'
'lb.3d3'
'lb.3dg'
'lb.3dt'
'left.pic'
'libya.spr'
'libya.wld'
'me.3d3'
'me.3dg'
'me.3dt'
'me.spr'
'me.wld'
'medal.pic'
'mgraphic.dem'
'mgraphic.exe'
'misc.exe'
'nc.3d3'
'nc.3dg'
'nc.3dt'
'nc.wld'
'ncape.spr'
'nsound.exe'
'persian.spr'
'pg.3d3'
'pg.3dg'
'pg.3dt'
'photo.3d3'
'promo.pic'
'read.me'
'rear.pic'
'right.pic'
'rsound.exe'
'start.exe'
'su.exe'
'tgraphic.exe'
'title16.pic'
'title640.pic'
'tsound.exe'
'vn.3d3'
'vn.3dg'
'vn.3dt'
'vn.spr'
'vn.wld'
'wall.pic'
)

# construct game items array
items=()

# iterate over known files
for f in "${files[@]}"; do
	file=$f
	[ -f "$file" ] || file=$(echo $file | tr '[:lower:]' '[:upper:]')
	[ -f "$file" ] || { items+=("$(printf "%-12s: [missing]\n" $f)"); continue; }
	debug "File $file"
	size=$(wc -c "$file" | cut -d ' ' -f 1)
	debug "size $size"
	md5=$(md5sum "$file" | cut -d ' ' -f 1)
	debug "md5 $md5"
	extra=''
	case $f in
		*.exe)
			lzexe=$(file "$file" | grep 'LZEXE')
			[ -z "$lzexe" ] || extra='[lzexe] '
			;;
		read.me)
			ver=$(head -n1 "$file" | sed -e 's|.*F15 Strike Eagle II - \([0-9]\+\.[0-9]\+\).*|\1|')
			debug "ver '${ver}'"
			if [ -z "$ver" ]; then extra='[ver ???]';
			else extra="[ver $ver] "; 
			fi
			debug "extra '$extra'"
			;;
	esac
	string=$(printf "%-12s: %06d %s %s\n" "$f" "$size" "$md5" "$extra")
	items+=("$string")
done

# sort result of known files scan to preserve order for comparisons
readarray -t sorted < <(for a in "${items[@]}"; do echo "$a"; done | sort)

log='fingerprint.log'

# iterate over directory contents to find unknown files, add at the end of the output
# ignore files matching known files and the logfile
for f in *; do
	[ "$f" == "$log" ] && continue
	contains "$f" "${files[@]}" && continue
	lowercase=$(echo $f | tr '[:upper:]' '[:lower:]')
	contains "$lowercase" "${files[@]}" && continue
	sorted+=("$f: [unknown]")
done

# save to logfile and print to screen
>$log
for i in "${sorted[@]}"; do
	echo "$i" >> $log
	echo "$i"
done

debug "exiting dir"
popd &> /dev/null
