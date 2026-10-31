#!/bin/bash
# The branches of the savegame readers and writers (and of 0x4D6780) that the
# saves don't reach, on a retouched save (tools/oracle/write_retouch.cpp):
# the game's readers load it and tick_probe --carga compares that with
# p3data's load of it; then the game (write_oracle.py, which runs 0x4D6780
# for each merchant first) and p3data (p3save write) save it again and
# compare_written.py compares them.
#
# usage: scripts/write-retouched.sh [SAVE]   (by default 1)
# Needs the same as scripts/write-reload.sh.
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
s=${1:-1}
r="$tmp/$s-retouched"
"$build/tools/oracle/write_retouch" "$P3_CPR" "missav/$s.pat" "$r.pat"
"$build/tools/p3save/p3save" "$P3_CPR" "$r.pat" raw "$r.raw"
python3 tools/oracle/town_oracle.py "$P3_EXE" "$r.raw" "$r.probe" 0 51ba10 --barcos "$P3_SHIPS" --cpr "$P3_CPR" >/dev/null
"$build/tools/oracle/tick_probe" "$r.probe" --carga "$r.raw" --cpr "$P3_CPR" | tail -12
python3 tools/oracle/write_oracle.py "$P3_EXE" "$r.raw" "$r-game.raw" --cpr "$P3_CPR" >/dev/null
"$build/tools/p3save/p3save" "${fix[@]}" "$P3_CPR" "$r.pat" write "$r-again.pat" "$r-p3vita.raw" >/dev/null
status=0
python3 tools/oracle/compare_written.py "$r-game.raw" "$r-p3vita.raw" || status=1
rm -f "$r.pat" "$r.raw" "$r.probe" "$r-game.raw" "$r-again.pat" "$r-p3vita.raw"
exit $status
