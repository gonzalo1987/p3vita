#!/bin/bash
# Check 2 of the savegame writer: the whole world for DAYS days with the
# game's code (town_oracle.py, the pieces of world.sh) and with p3data
# (tick_probe --continuo --carga), and then both write the save (--write);
# compare_written.py compares them byte for byte, and their .pst too; then the
# game and p3vita load p3vita's .pat and .pst again (tick_probe --carga
# --pst). The saves run in parallel.
#
# usage: scripts/write-after-days.sh DAYS [SAVES...]   (by default 1 3 7)
# P3_FIX_GAME_BUGS=1: p3vita's reload of the .pat and the .pst with the fixes
# to the game's bugs (only the load: the days and the writing stay the
# game's, since there the fixes would change the simulation).
# Needs the same as scripts/write-reload.sh.
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
fix=()
if [ -n "${P3_FIX_GAME_BUGS:-}" ]; then
	fix=(--fix-game-bugs)
fi
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
	# The .pst (the daily statistics): byte for byte, and then the game's
	# readers load p3vita's .pat and .pst and tick_probe --carga compares that
	# with what p3vita reads from them.
	if cmp -s "$tmp/$s-after-game.raw.pst" "$tmp/$s-after-p3vita.raw.pst"; then
		echo ".pst: same bytes ($(stat -c %s "$tmp/$s-after-p3vita.raw.pst"))"
	else
		echo ".pst: different ($(stat -c %s "$tmp/$s-after-game.raw.pst") bytes in the game, $(stat -c %s "$tmp/$s-after-p3vita.raw.pst") in p3vita)"
		status=1
	fi
	python3 tools/oracle/town_oracle.py "$P3_EXE" "$tmp/$s-after-p3vita.raw" "$tmp/$s-after-reload.probe" 0 51ba10 \
		--barcos "$P3_SHIPS" --cpr "$P3_CPR" --pst "$tmp/$s-after-p3vita.raw.pst" >/dev/null
	echo "reload of the .pat and the .pst: $("$build/tools/oracle/tick_probe" "$tmp/$s-after-reload.probe" --carga \
		"$tmp/$s-after-p3vita.raw" --pst "$tmp/$s-after-p3vita.raw.pst" --cpr "$P3_CPR" "${fix[@]}" | tail -1)"
	rm -f "$tmp/$s-after-reload.probe" "$tmp/$s-after-game.raw" "$tmp/$s-after-p3vita.raw" "$tmp/$s-after-game.raw.pst" \
		"$tmp/$s-after-p3vita.raw.pst" "$tmp/$s-after.txt" "$tmp/$s-after.err"
done
exit $status
