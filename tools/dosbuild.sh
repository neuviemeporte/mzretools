#!/bin/bash
#
# This script invokes development binaries (compiler, linker, assembler) in DOSBox.
#
# TODO: 
# - linking can be simplified by using CL instead of LINK
# - get rid of infile/outfile, just build in-tree? then make install copies to the output dir

TOOLCHAIN_DIR=dos
CONF_DIR=conf
CONF_FILE=$CONF_DIR/toolchain.conf
DEBUG=1
# always print toolchain stdout
VERBOSE=1
cmdline=$@

function syntax() {
    [ "$1" ] && echo "Error: $1"
    echo "Syntax: dosbuild.sh cc|link|as toolchain -i infiles... -o outfile [-f flags...] [-l libs...]"
    exit 1
}

function debug() {
    if ((DEBUG)); then echo $1; fi
}

function fatal() {
    if ((DEBUG)); then echo $cmdline; fi
    echo "Error: $1"
    exit 1
}

function basedir() {
    echo $1 | cut -d '/' -f 1
}

function dossep() {
    echo "$1" | sed -e 's|/|\\|g'
}

which dosbox &> /dev/null || fatal "Dosbox not installed"
[ -f "$CONF_FILE" ] || fatal "Dosbox configuration file does not exist: $CONF_FILE"

# extract tool name (compiler/linker/assembler) and toolchain from cmdline
tool=$1
if [ "$tool" != "test" ]; then
    chain=$2
    { [ "$tool" ] && [ "$chain" ]; } || syntax
    shift 2
    # make sure the toolchain directory exists
    TOOLCHAIN_DIR+="/$chain"
    [ -d "$TOOLCHAIN_DIR" ] || fatal "Toolchain directory does not exist: $TOOLCHAIN_DIR"
    # determine tool executable name based on toolchain type (ms c, turbo c, ...)
    toolok=1
    if [[ $chain =~ ^msc ]]; then
        case $tool in
        cc) tool=cl;;
        link) ;;
        lib) ;;
        *) toolok=0;;
        esac
    elif [[ $chain =~ ^qc ]]; then
        case $tool in
        cc) tool=qcl;;
        link) 
            tool=link
            [ $chain = qc251 ] && tool=qlink
            ;;
        *) toolok=0;;
        esac
    elif [[ $chain =~ ^tc || $chain =~ ^tcpp ]]; then
        case $tool in
        cc) tool=tcc;;
        link) tool=tlink;;
        *) toolok=0;;
        esac
    elif [[ $chain =~ ^bcpp ]]; then
        case $tool in
        cc) tool=bcc;;
        link) tool=tlink;;
        *) toolok=0;;
        esac    
    elif [[ $chain =~ ^masm ]]; then
        case $tool in
        as) tool=masm;;
        *) toolok=0;;
        esac
    elif [[ $chain =~ ^wc ]]; then
        case $tool in
        cc) tool=wcc386;;
        *) toolok=0;;
        esac
    elif [[ $chain =~ ^tasm ]]; then
        case $tool in
        as) tool=tasm;;
        *) toolok=0;;
        esac
    else
        fatal "Toochain not recognized: $chain"
    fi
    ((toolok)) || fatal "Tool '$tool' not supported by toolchain '$chain'"
    tool_exe=$(find $TOOLCHAIN_DIR -iname "$tool.exe")
    debug "tool = $tool, chain = $chain, tool_exe = $tool_exe"
    [ -f "$tool_exe" ] || fatal "Unable to find '$tool.exe' in $TOOLCHAIN_DIR"
else
    debug "Running test application"
    shift 1
fi

# extract input, output and flags from cmdline
infiles=''
outfile=''
flags=''
libs=''
argtype=''
until [ -z "$1" ]; do
    arg=$1
    case $arg in
    -i) argtype=i
        ;;
    -o) argtype=o
        ;;
    -f) argtype=f
        ;;
    -l) argtype=l
        ;;
    *)
        case $argtype in
            i) [ -f "$arg" ] || syntax "Input file does not exist: $arg" 
               [ "$infiles" ] && infiles+=" "
               infiles+="$arg"
               ;;
            o) 
               [ "$outfile" ] && syntax "More than one output file on cmdline"
               outfile=$arg;
               ;;
            f) 
               [ "$flags" ] && flags+=" "
               flags+="$arg";
               ;;
            l) 
               [ "$libs" ] && libs+=" "
               libs+="$arg";
               ;;
            *) syntax "unrecognized argument: $arg";
        esac
        ;;
    esac
    shift
done
debug "in: '$infiles', out: '$outfile', flags: '$flags', libs: '$libs'"
[ "$infiles" ] || syntax "empty infiles"
if [ "$tool" != "test" ]; then
    [ "$outfile" ] || syntax "empty outfiles"
fi

# examine input and output base directories, must make them available in dosbox
infile_dir=''
infiles_dos=''
for f in $infiles; do
    [ -f "$f" ] || { echo "Input file $f does not exist!"; exit 1; }
    curdir=$(basedir $f)
    if [ "$infile_dir" ]; then
        [ $curdir != $infile_dir ] && fatal "Input file $f not in input directory $infile_dir"
    else
        infile_dir=$curdir
    fi
    [ "$infiles_dos" ] && infiles_dos+=" "
    infiles_dos+="$(dossep ${f#${infile_dir}/})"
done
[ -f "$outfile" ] && rm $outfile # remove output file if it exists from the previous run, we use its existence to check for success/failure
outfile_dir=$(basedir $outfile)
outfile_name=$(basename $outfile)
outfile_noext="${outfile_name%.*}"
rspname=${outfile_noext}.rsp
outfile_drive='f'
outfile_dos="${outfile_drive}:\\$(dossep ${outfile#${outfile_dir}/})"

if [ "$tool" != "test" ]; then
    [ -d "$infile_dir" ] || fatal "Input directory does not exist: $infile_dir"
    [ -d "$outfile_dir" ] || fatal "Output directory does not exist: $outfile_dir"
fi

# copy infiles to temporary directory, enables parallell builds
tmpdir=$(mktemp -d XXXXXXXX)
function cleanup() {
    debug "Cleaning up tmpdir: $tmpdir"
    rm -rf $tmpdir
}
trap cleanup EXIT
pushd "$infile_dir" &> /dev/null || fatal "Unable to change to input directory $infile_dir"
cp $infiles_dos ../$tmpdir  || fatal "Unable to copy files to temporary directory $tmpdir"
popd &> /dev/null
infile_rsp=$tmpdir/$rspname
debug "infile_dir = '$infile_dir', infiles_dos = '$infiles_dos', outfile_dir = '$outfile_dir', outfile_dos = '$outfile_dos', infile_rsp = '$infile_rsp'"

# compose tool cmdline in dos based on tool type
cmdline=$tool
case $tool in
    cl|qcl)
        [ "$flags" ] && cmdline+=" $flags"
        cmdline+=" /c /Fo$outfile_dos $infiles_dos"
        ;;
    tcc|bcc)
        [ "$flags" ] && cmdline+=" $flags"
        # compile
        [[ $outfile_dos =~ \.(obj|OBJ) ]] && cmdline+=" -c -o$outfile_dos"
        # link
        [[ $outfile_dos =~ \.(exe|EXE) ]] && cmdline+=" -e$outfile_dos -LC:\\$chain\lib"
        cmdline+=" $infiles_dos"
        ;;        
    link|qlink)
        [ "$flags" ] && cmdline+=" $flags"
        # build response file, get around cmdline length limit
        > $infile_rsp
        count=0
        for o in $infiles_dos; do
            echo -n "$o" >> $infile_rsp
            if ((++count == 8)); then
                echo "+" >> $infile_rsp
                count=0
            else
                echo -n " " >> $infile_rsp
            fi
        done
        if ((count != 8)); then
            echo "" >> $infile_rsp
        fi
        echo "$outfile_dos" >> $infile_rsp
        echo ";" >> $infile_rsp
        # TODO: wrap libs with + too?
        if [ "$libs" ]; then
            echo $libs >> $infile_rsp
        else
            echo ";" >> $infile_rsp
        fi
        if ((DEBUG)); then
        echo "--- $infile_rsp:"
        cat $infile_rsp
        echo "---"
        fi
        cmdline+=" @${rspname}"
        ;;
    lib)
        echo -n "$outfile_dos" > $infile_rsp
        if [ "$flags" ]; then
            echo -n " $flags" >> $infile_rsp
        fi
        count=$(echo $infiles_dos | wc -w)
        idx=1
        for o in $infiles_dos; do
            if ((idx != count)); then
                echo "+$o&" >> $infile_rsp
            else
                echo "+$o" >> $infile_rsp
            fi
            ((++idx))
        done
        echo ";" >> $infile_rsp
        if ((DEBUG)); then
        echo "--- $infile_rsp:"
        cat $infile_rsp
        echo "---"
        fi
        cmdline+=" @${rspname}"
        ;;
    tlink)
        compiler_dir=$TC_DIR
        [ "$flags" ] && cmdline+=" $flags"
        fatal "tlink not implemented"
        # cmdline+=" $infiles_dos,$outfile_dos,,,"
        ;;        
    masm)
        compiler_dir=$MASM_DIR
        [ "$flags" ] && cmdline+=" $flags"
        cmdline+=" $infiles_dos,$outfile_dos,,;"
        ;;
    tasm)
        compiler_dir=$TC_DIR
        [ "$flags" ] && cmdline+=" $flags"
        cmdline+=" $infiles_dos,$outfile_dos,,;"
        ;;        
    wcc386)
        [ "$flags" ] && cmdline+=" $flags"
        cmdline+=" /fo=$outfile_dos $infiles_dos"    
        ;;
    test)
        cmdline=$infiles_dos
        ;;
    *)
        echo "Unrecognized tool: $tool";
        exit 1
        ;;
esac
debug "cmdline: $cmdline"

# create dos bat file for launching inside the emulator
# mounts following drives:
# drive C: - toolchain
# drive D: - original input directory (for headers etc.)
# drive E: - temporary directory with input files
# drive F: - output directory
BAT_FILE=$tmpdir/build.bat
cat > $BAT_FILE <<EOF
set PATH=Z:\;C:\\$chain\\bin;C:\\$chain\binb;C:\\$chain\bound;C:\\$chain
set INCLUDE=C:\\$chain\\include
set LIB=C:\\$chain\\lib
$cmdline > log.txt
EOF

if ((DEBUG)); then 
    echo "--- $BAT_FILE"
    cat $BAT_FILE; 
    echo "---"
fi

INSTANCE_CONF=$tmpdir/instance.conf
cat > $INSTANCE_CONF << EOF
[autoexec]
mount c dos
mount d $infile_dir
mount e $tmpdir
mount f $outfile_dir
e:
build.bat
exit
EOF

# remove logfile from previous run if exists to avoid reporting bogus errors in case of build failure
logfile=$tmpdir/log.txt
emu_logfile=$tmpdir/dosbox.log
rm -f $logfile
rm -f $emu_logfile
# start bat file in emulator in headless mode
[ "$tool" != "test" ] && echo "$cmdline"
debug "Launching headless dosbox to run $BAT_FILE"
SDL_VIDEODRIVER=dummy dosbox -conf $CONF_FILE -conf $INSTANCE_CONF &> $emu_logfile
debug "Dosbox returned $?"
# check if successful by examining if output file exists
if [ "$tool" != "test" ]; then
    if [ ! -f "$outfile" ]; then
        debug "Output file $outfile does not exist"
        if [ -f "$logfile" ]; then
            debug "Printing logfile"
            cat $logfile; 
        else
            debug "Printing emulator logfile"
            cat $emu_logfile
            echo "Build failed and no logfile found, check emulator configuration"
        fi
        exit 1;
    fi
    # trick to get rid of uppercase filename on WSL
    outfile_tmp="${outfile}.tmp"
    mv "$outfile" "$outfile_tmp"
    mv "$outfile_tmp" "$outfile"
    # the linker can create an output file even in presence of errors so check log
    if grep -i "error" $logfile &> /dev/null; then
        debug "Error detected in $logfile"
        rm $outfile
        cat $logfile; 
        exit 1;
    fi
    if grep -ie "warning" $logfile &> /dev/null || ((VERBOSE)); then
        debug "Warning detected in $logfile"
        cat $logfile;
    fi
else
    cat $logfile
    grep -ie "failed" $logfile &> /dev/null && exit 1
fi

debug "success"
exit 0