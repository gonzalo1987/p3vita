#!/bin/bash
# Runs scenarios of retouch_unchecked.py in some saves (4 at a time) and
# repeats each recording with tick_probe, call by call.
#
# usage: tools/oracle/unchecked.sh SCENARIO[:SAVES] ...
#   SAVES  comma-separated (1,3,tut2); default: $SAVES or "1 3"
# Variables: P3_EXE, P3_CPR, P3_SHIPS (the folder with navdata/ and
# scripts/StadtDaten.ini), P3_BUILD (the build with tools/oracle/tick_probe,
# default build), P3_TMP (the decompressed saves, default /tmp), KEEP=1
# keeps the recordings, TAIL (lines of each log, default 40), PROBE_ARGS
# (more options for tick_probe, e.g. --fix-game-bugs).
set -u
cd "$(dirname "$0")/../.."
: "${P3_EXE:?P3_EXE missing (path of Patrician3.exe)}"
: "${P3_CPR:?P3_CPR missing (path of p2arch0_eng.cpr)}"
: "${P3_SHIPS:?P3_SHIPS missing (folder with navdata/ and scripts/)}"
build=${P3_BUILD:-build}
tmp=${P3_TMP:-/tmp}
out=$tmp/unchecked
mkdir -p "$out"
start=$(date +%s)
jobs=()
for arg in "$@"; do
	scenario=${arg%%:*}
	saves=${SAVES:-1 3}
	[ "$arg" != "$scenario" ] && saves=${arg#*:}
	for s in ${saves//,/ }; do
		jobs+=("$scenario $s")
	done
done
running=0
for job in "${jobs[@]}"; do
	set -- $job
	python3 tools/oracle/retouch_unchecked.py "$P3_EXE" "$tmp/$2.raw" "$out/$2-$1.probe" "$1" \
		--barcos "$P3_SHIPS" --cpr "$P3_CPR" > "$out/$2-$1.log" 2>&1 &
	running=$((running + 1))
	if [ $running -ge 4 ]; then
		wait -n
		running=$((running - 1))
	fi
done
wait
status=0
for job in "${jobs[@]}"; do
	set -- $job
	echo "== $2 $1"
	tail -n "${TAIL:-40}" "$out/$2-$1.log"
	grep -q "MISSING\|Traceback\|Error" "$out/$2-$1.log" && status=1
	if [ -f "$out/$2-$1.probe" ]; then
		"$build/tools/oracle/tick_probe" "$out/$2-$1.probe" --cpr "$P3_CPR" ${PROBE_ARGS:-} 2>&1 | tail -n 12 | tee "$out/$2-$1.cmp"
		grep -q "[1-9][0-9]* distintas" "$out/$2-$1.cmp" && status=1
		[ -n "${KEEP:-}" ] || rm -f "$out/$2-$1.probe"
	fi
done
echo "time: $(($(date +%s) - start)) s"
exit $status
