#!/bin/bash
# Writes each save with the game's code (tools/oracle/write_oracle.py) and with
# p3data (p3save write) and compares the uncompressed files byte for byte
# (tools/oracle/compare_written.py) and the compressed ones.
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
	if cmp -s "$tmp/$s-game-written.pat" "$tmp/$s-written.pat"; then
		echo "compressed: equal ($(stat -c %s "$tmp/$s-written.pat") bytes)"
	else
		echo "compressed: different ($(stat -c %s "$tmp/$s-game-written.pat") bytes in the game, $(stat -c %s "$tmp/$s-written.pat") in p3vita)"
	fi
	rm -f "$tmp/$s-game-written.raw" "$tmp/$s-written.raw"
done
exit $status
