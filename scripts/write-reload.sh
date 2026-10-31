#!/bin/bash
# Round trip of the saves p3data writes: p3save write, then the game's reader
# loads the written file in the emulator (town_oracle.py, a recording of 0
# days) and tick_probe --carga compares it with what p3data reads from the
# same file (0 differences expected).
#
# usage: scripts/write-reload.sh [SAVES...]   (by default the 12 v0x82 ones of missav/)
# Needs the same as scripts/write-check.sh, and P3_SHIPS (a folder with
# navdata/ and scripts/StadtDaten.ini) like scripts/tick-probe.sh.
set -e
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
# P3_FIX_GAME_BUGS=1: p3vita writes with the fixes to the game's bugs
# (game_fixes.hpp); the differences left are then those the fixes make.
fix=()
if [ -n "${P3_FIX_GAME_BUGS:-}" ]; then
	fix=(--fix-game-bugs)
fi
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 2 3 4 5 6 7 tut0 tut1 tut2 tut3 tut4)
status=0
for s in "${saves[@]}"; do
	"$build/tools/p3save/p3save" "${fix[@]}" "$P3_CPR" "missav/$s.pat" write "$tmp/$s-written.pat" >/dev/null
	"$build/tools/p3save/p3save" "$P3_CPR" "$tmp/$s-written.pat" raw "$tmp/$s-written.raw"
	python3 tools/oracle/town_oracle.py "$P3_EXE" "$tmp/$s-written.raw" "$tmp/$s-written.probe" 0 51ba10 \
		--barcos "$P3_SHIPS" --cpr "$P3_CPR" >/dev/null
	echo "== $s: $("$build/tools/oracle/tick_probe" "$tmp/$s-written.probe" --carga "$tmp/$s-written.raw" --cpr "$P3_CPR" | tail -1)" ||
		status=1
	rm -f "$tmp/$s-written.probe" "$tmp/$s-written.raw" "$tmp/$s-written.pat"
done
exit $status
