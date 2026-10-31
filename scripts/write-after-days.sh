#!/bin/bash
# Check 2 of the savegame writer: the whole world for DAYS days with the
# game's code (town_oracle.py, the pieces of world.sh) and with p3data
# (tick_probe --continuo --carga), and then both write the save (--write);
# compare_written.py compares them byte for byte. The saves run in parallel.
#
# usage: scripts/write-after-days.sh DAYS [SAVES...]   (by default 1 3 7)
# Needs the same as scripts/write-reload.sh.
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
days=${1:?missing DAYS}
shift
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 3 7)
functions=51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0
for s in "${saves[@]}"; do
	(
		[ -f "$tmp/$s.raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" raw "$tmp/$s.raw" >/dev/null
		python3 tools/oracle/town_oracle.py "$P3_EXE" "$tmp/$s.raw" "$tmp/$s-after.probe" "$days" "$functions" \
			--barcos "$P3_SHIPS" --cpr "$P3_CPR" --write "$tmp/$s-after-game.raw" >/dev/null 2>"$tmp/$s-after.err"
		"$build/tools/oracle/tick_probe" "$tmp/$s-after.probe" --continuo --carga "$tmp/$s.raw" --cpr "$P3_CPR" \
			--max 8 --write "$tmp/$s-after-p3vita.raw" >"$tmp/$s-after.txt" 2>&1
		rm -f "$tmp/$s-after.probe"
	) &
done
wait
status=0
for s in "${saves[@]}"; do
	echo "== $s: $(grep -o '[0-9]* distintas' "$tmp/$s-after.txt" | awk '{n+=$1} END {print n+0}') calls different $(head -c 300 "$tmp/$s-after.err")"
	python3 tools/oracle/compare_written.py "$tmp/$s-after-game.raw" "$tmp/$s-after-p3vita.raw" || status=1
done
exit $status
