#!/bin/bash
# The ticker lines of p3vita (letters.hpp, compose_ticker) against the game's
# (0x5483D0 in the emulator, tools/oracle/ticker_oracle.py), for every kind
# with arguments taken from each save's world. With P3_EXE_SPA (the Spanish
# Patrician3.exe), the same with the Spanish exe's code and texts
# (compare_ticker.py: the pairs that differ, decoded).
#
# usage: scripts/ticker-check.sh [SAVES...]   (by default the 12 of missav/)
# Needs P3_EXE, P3_CPR, P3_SHIPS (as scripts/letters-check.sh); BUILD.
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 2 3 4 5 6 7 tut0 tut1 tut2 tut3 tut4)
languages=(eng)
[ -n "${P3_EXE_SPA:-}" ] && languages+=(spa)
for s in "${saves[@]}"; do
	for lang in "${languages[@]}"; do
		(
			raw=$tmp/$s.raw
			[ -f "$raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" raw "$raw" >/dev/null
			exe=$P3_EXE
			spanish=()
			if [ $lang = spa ]; then exe=$P3_EXE_SPA; spanish=(--spanish "$P3_EXE_SPA"); fi
			python3 tools/oracle/ticker_oracle.py "$P3_EXE" "$raw" "$tmp/$s-ticker-game-$lang.txt" --cpr "$P3_CPR" \
				--barcos "$P3_SHIPS" "${spanish[@]}" >/dev/null 2>"$tmp/$s-ticker-$lang.err"
			"$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" ticker "$exe" "$tmp/$s-ticker-game-$lang.txt" \
				>"$tmp/$s-ticker-p3vita-$lang.txt" 2>>"$tmp/$s-ticker-$lang.err"
		) &
	done
done
wait
status=0
for s in "${saves[@]}"; do
	for lang in "${languages[@]}"; do
		echo "== $s ($([ $lang = spa ] && echo Spanish || echo English))"
		head -n 2 "$tmp/$s-ticker-$lang.err" | cut -c1-150
		python3 tools/oracle/compare_ticker.py "$tmp/$s-ticker-game-$lang.txt" "$tmp/$s-ticker-p3vita-$lang.txt" ||
			status=1
	done
done
exit $status
