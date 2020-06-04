#!/bin/bash
set -e

if [ ! -n "${srcdir}" ]; then
    srcdir="."
fi

orig=${srcdir}/ia32_test_insn.S
obj=$(mktemp -t obj-XXXXXX)
src=$(mktemp -t src-XXXXXX)
trap "rm -f $obj $src" EXIT ERR

cat "$orig" > "$src"
if [ "x$1" = "x--try-xfail" ]; then
    shift || true
    perl -pi -e 's/^([^#]+)#.*XFAIL.*$/$1/;' "$src"
fi
XFAIL=$(grep XFAIL "$src" || true)

INPUT=$(grep -v '^#' $src | perl -pe 's/^[^#]+#\s*([^#]*).*$/$1/;' | tr A-Z a-z | sed -e 's/ $//g')
OUTPUT=$(${srcdir}/asmdisasm.pl "$src" "$obj" | cut -d'#' -f1 | sed -e 's/ $//g')

OKAY=$(echo "$INPUT" | wc -l)
XFAILED=$(echo "$XFAIL" | wc -l)
DIFF=$(diff -u <(echo "$INPUT") <(echo "$OUTPUT") || true)
REPORT=$(echo "$DIFF" | grep '^-[^-]' | cut -c2-)
BAD=$(echo "$REPORT" | wc -l)
OKAY=$(( OKAY - BAD - XFAILED ))

if [ "x$1" = "x--diff" ]; then
    echo "$DIFF"
    if [ -n "$REPORT" ]; then
        exit 1
    else
        exit 0
    fi
fi

echo "== Start Instruction Assemble/Disassemble Report =="

if [ -n "$XFAIL" ]; then
    echo "=== Expected Failures ==="
    echo "$XFAIL"
fi
if [ -n "$REPORT" ]; then
    echo "=== Failures ==="
    echo "$DIFF"
fi

echo "=== Test Summary ==="

if [ -n "$XFAIL" ]; then
    echo "XFAIL: $XFAILED"
fi

echo "ok: $OKAY"

if [ -n "$REPORT" ]; then
    echo "FAILED: $BAD"
    exit 1
fi

echo "== End Instruction Assemble/Disassemble Test =="
