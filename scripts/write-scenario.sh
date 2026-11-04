#!/bin/bash
# The savegame writer after a retouched state with new letters: a scenario of
# tools/oracle/retouch_mail.py runs DAYS days with the game's code, recording
# FUNCS, and the game saves the state reached (--write). tick_probe compares
# the recording call by call (the letters p3vita sends, their buffers too).
# Then the game (write_oracle.py) and p3data (p3save write) load that save
# and write it again, and compare_written.py compares both byte for byte (not
# with the first save: the load recomputes some fields, such as the price
# thresholds, so a save written from memory after some days is not what a
# load and a save give); last, the game's readers load p3data's file and
# tick_probe --carga compares that with p3data's load.
#
# usage: scripts/write-scenario.sh SCENARIO DAYS [SAVES...]   (by default 1 3 5)
# FUNCS (default 4d85c0,535760) are the functions recorded. Needs the same as
# scripts/write-reload.sh. With RETOUCH=unchecked the scenario is one of
# tools/oracle/retouch_unchecked.py instead.
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
scenario=${1:?missing SCENARIO}
days=${2:?missing DAYS}
shift 2
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 3 5)
for s in "${saves[@]}"; do
	(
		r="$tmp/$s-$scenario-w"
		[ -f "$tmp/$s.raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" raw "$tmp/$s.raw" >/dev/null
		if [ "${RETOUCH:-mail}" = unchecked ]; then
			python3 tools/oracle/retouch_unchecked.py "$P3_EXE" "$tmp/$s.raw" "$r.probe" "$scenario" --days "$days" \
				--functions "${FUNCS:-4d85c0,535760}" --barcos "$P3_SHIPS" --cpr "$P3_CPR" --write "$r-game.raw" \
				>"$r.log" 2>&1
		else
			python3 tools/oracle/retouch_mail.py "$P3_EXE" "$tmp/$s.raw" "$r.probe" "$days" "$scenario" \
				--functions "${FUNCS:-4d85c0,535760}" --barcos "$P3_SHIPS" --cpr "$P3_CPR" --write "$r-game.raw" \
				>"$r.log" 2>&1
		fi
		"$build/tools/oracle/tick_probe" "$r.probe" --cpr "$P3_CPR" --names "$tmp/$s.raw" --max 8 >"$r.txt" 2>&1
		rm -f "$r.probe"
	) &
done
wait
status=0
for s in "${saves[@]}"; do
	r="$tmp/$s-$scenario-w"
	echo "== $s $scenario: $(grep -o '[0-9]* distintas' "$r.txt" | awk '{n+=$1} END {print n+0}') calls different"
	grep -q "Traceback\|Error" "$r.log" && { tail -5 "$r.log"; status=1; continue; }
	python3 tools/oracle/write_oracle.py "$P3_EXE" "$r-game.raw" "$r-game2.raw" --cpr "$P3_CPR" >/dev/null
	"$build/tools/p3save/p3save" "$P3_CPR" "$r-game.raw.pat" write "$r-again.pat" "$r-p3vita.raw" >/dev/null
	python3 tools/oracle/compare_written.py "$r-game2.raw" "$r-p3vita.raw" || status=1
	python3 tools/oracle/town_oracle.py "$P3_EXE" "$r-p3vita.raw" "$r-reload.probe" 0 51ba10 \
		--barcos "$P3_SHIPS" --cpr "$P3_CPR" >/dev/null
	echo "reload of p3data's .pat: $("$build/tools/oracle/tick_probe" "$r-reload.probe" --carga "$r-p3vita.raw" \
		--cpr "$P3_CPR" | tail -1)"
	rm -f "$r-reload.probe" "$r-game.raw" "$r-game2.raw" "$r-game.raw.pat" "$r-game.raw.pst" "$r-again.pat" "$r-p3vita.raw" \
		"$r.txt" "$r.log"
done
exit $status
