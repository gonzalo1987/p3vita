#!/bin/bash
# The letters' texts of p3vita (letters.hpp) against the game's composer
# (0x47F3B0 in the emulator, tools/oracle/letter_oracle.py), for the letters
# of the human player's chain in each save: compare_letters.py, by type.
# With P3_EXE_SPA (the Spanish Patrician3.exe), the same with the Spanish
# exe's composer (letter_oracle.py --spanish) and p3vita with its texts.
#
# usage: scripts/letters-check.sh [SAVES...]   (by default the 12 of missav/)
# A save given as a path ending in .raw (with its .pat next to it, as
# retouch_mail.py --write leaves them) is used as it is.
# Needs P3_EXE, P3_CPR, P3_SHIPS (as scripts/write-reload.sh); BUILD.
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
			if [[ $s == *.raw ]]; then raw=$s; pat=$s.pat; name=$(basename "$s" .raw); else
				raw=$tmp/$s.raw; pat=missav/$s.pat; name=$s
				[ -f "$raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "$pat" raw "$raw" >/dev/null
			fi
			exe=$P3_EXE
			spanish=()
			if [ $lang = spa ]; then exe=$P3_EXE_SPA; spanish=(--spanish "$P3_EXE_SPA"); fi
			python3 tools/oracle/letter_oracle.py "$P3_EXE" "$raw" "$tmp/$name-letters-game-$lang.txt" --cpr "$P3_CPR" \
				--barcos "$P3_SHIPS" "${spanish[@]}" >/dev/null 2>"$tmp/$name-letters-$lang.err"
			"$build/tools/p3save/p3save" "$P3_CPR" "$pat" letters "$exe" >"$tmp/$name-letters-p3vita-$lang.txt" \
				2>>"$tmp/$name-letters-$lang.err"
		) &
	done
done
wait
status=0
for s in "${saves[@]}"; do
	name=$s
	[[ $s == *.raw ]] && name=$(basename "$s" .raw)
	for lang in "${languages[@]}"; do
		echo "== $name ($([ $lang = spa ] && echo Spanish || echo English))"
		head -c 300 "$tmp/$name-letters-$lang.err"
		python3 tools/oracle/compare_letters.py "$tmp/$name-letters-game-$lang.txt" "$tmp/$name-letters-p3vita-$lang.txt" ||
			status=1
	done
done
exit $status
