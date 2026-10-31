#!/bin/bash
# Writes each save with the game's code (tools/oracle/write_oracle.py) and with
# p3data (p3save write) and compares the uncompressed files byte for byte
# (tools/oracle/compare_written.py); then compresses the game's uncompressed
# file with p3data (p3save compress) and compares it with the game's
# compressed one, and decompresses p3data's .pat (p3save raw of the file).
#
# usage: scripts/write-check.sh [SAVES...]   (by default the 12 v0x82 ones of missav/)
# Needs: build/ (or BUILD=<dir>) built, unicorn for python3 and the variables
# P3_EXE (Patrician3.exe) and P3_CPR (p2arch0_eng.cpr). The decompressed saves
# and the written files go to ${P3_TMP:-/tmp}.
set -e
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 2 3 4 5 6 7 tut0 tut1 tut2 tut3 tut4)
status=0
for s in "${saves[@]}"; do
	[ -f "$tmp/$s.raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" raw "$tmp/$s.raw" >/dev/null
	python3 tools/oracle/write_oracle.py "$P3_EXE" "$tmp/$s.raw" "$tmp/$s-game-written.raw" \
		--packed "$tmp/$s-game-written.pat" --cpr "$P3_CPR" >/dev/null
	"$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" write "$tmp/$s-written.pat" "$tmp/$s-written.raw" >/dev/null
	echo "== $s"
	python3 tools/oracle/compare_written.py "$tmp/$s-game-written.raw" "$tmp/$s-written.raw" || status=1
	"$build/tools/p3save/p3save" compress "$tmp/$s-game-written.raw" "$tmp/$s-recompressed.pat"
	if cmp -s "$tmp/$s-game-written.pat" "$tmp/$s-recompressed.pat"; then
		echo "compressor: same bytes as the game's ($(stat -c %s "$tmp/$s-recompressed.pat"))"
	else
		echo "compressor: different ($(stat -c %s "$tmp/$s-game-written.pat") bytes in the game, $(stat -c %s "$tmp/$s-recompressed.pat") in p3vita)"
		status=1
	fi
	if "$build/tools/p3save/p3save" "$P3_CPR" "$tmp/$s-written.pat" raw "$tmp/$s-unpacked.raw" 2>/dev/null &&
		cmp -s "$tmp/$s-unpacked.raw" "$tmp/$s-written.raw"; then
		echo "p3vita's .pat decompresses to its .raw"
	else
		echo "p3vita's .pat does NOT decompress to its .raw"
		status=1
	fi
	rm -f "$tmp/$s-game-written.raw" "$tmp/$s-written.raw" "$tmp/$s-recompressed.pat" "$tmp/$s-unpacked.raw"
done
exit $status
