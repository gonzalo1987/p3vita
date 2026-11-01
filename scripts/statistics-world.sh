#!/bin/bash
# The whole world with the daily statistics (on by default in town_oracle.py,
# which then records 0x54DE90 too), as re/emu/sesion5/world.sh in
# p3vita-datos: the oracle and tick_probe --continuo --carga for each save, in
# parallel, and the count of different calls. OPTS adds options to the oracle
# (OPTS=--no-statistics: without them).
#
# usage: scripts/statistics-world.sh DAYS [SAVES...]   (by default 1 2 3 4)
# Needs P3_EXE, P3_CPR and P3_SHIPS as scripts/write-reload.sh; BUILD.
cd "$(dirname "$0")/.."
: "${P3_EXE:?missing P3_EXE (path of Patrician3.exe)}"
: "${P3_CPR:?missing P3_CPR (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?missing P3_SHIPS (folder with navdata/ and scripts/)}"
tmp=${P3_TMP:-/tmp}
build=${BUILD:-build}
days=${1:?missing DAYS}
shift
saves=("$@")
[ ${#saves[@]} -eq 0 ] && saves=(1 2 3 4)
functions=${FUNCS:-51ba10,4f7bb0,506720,535760,5315c0,535450,52d7d0,6349f0,633a90,4d85c0,54de90}
start=$(date +%s)
for s in "${saves[@]}"; do
	(
		[ -f "$tmp/$s.raw" ] || "$build/tools/p3save/p3save" "$P3_CPR" "missav/$s.pat" raw "$tmp/$s.raw" >/dev/null
		python3 tools/oracle/town_oracle.py "$P3_EXE" "$tmp/$s.raw" "$tmp/$s-statistics.probe" "$days" "$functions" \
			--barcos "$P3_SHIPS" --cpr "$P3_CPR" $OPTS >/dev/null 2>"$tmp/$s-statistics.err"
		"$build/tools/oracle/tick_probe" "$tmp/$s-statistics.probe" --continuo --carga "$tmp/$s.raw" --cpr "$P3_CPR" \
			--max 8 >"$tmp/$s-statistics.txt" 2>&1
		rm -f "$tmp/$s-statistics.probe"
	) &
done
wait
status=0
for s in "${saves[@]}"; do
	different=$(grep -o '[0-9]* distintas' "$tmp/$s-statistics.txt" | awk '{n+=$1} END {print n+0}')
	echo "== $s: $(grep -c 'distintas' "$tmp/$s-statistics.txt") functions; different: $different; skipped: $(grep -o '[0-9]* llamadas saltadas' "$tmp/$s-statistics.txt" | awk '{n+=$1} END {print n+0}') $(head -c 300 "$tmp/$s-statistics.err")"
	grep -v "llamadas iguales\|saltadas" "$tmp/$s-statistics.txt" | head -6
	[ "$different" = 0 ] || status=1
	rm -f "$tmp/$s-statistics.txt" "$tmp/$s-statistics.err"
done
echo "time: $(( $(date +%s) - start )) s"
exit $status
