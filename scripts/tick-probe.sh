#!/bin/bash
# Graba con el oráculo de ciudades (tools/oracle/town_oracle.py) las llamadas a
# unas funciones del juego en varias partidas y las repite con tick_probe.
#
# uso: scripts/tick-probe.sh FUNCIONES [DÍAS] [PARTIDAS...] [-- opciones de tick_probe]
#   FUNCIONES  direcciones en hexadecimal separadas por comas (51ba10,527d40...)
#   DÍAS       días de juego (4 por omisión)
#   PARTIDAS   de missav/ (por omisión las 12 v0x82: 1..7 y tut0..4)
#
# Necesita: build/ compilado (cmake --build build), unicorn para python3 y
# las variables P3_EXE (Patrician3.exe) y P3_CPR (p2arch0_eng.cpr). Las
# partidas descomprimidas y las grabaciones van a ${P3_TMP:-/tmp}.
set -e
cd "$(dirname "$0")/.."
: "${P3_EXE:?falta P3_EXE (ruta de Patrician3.exe)}"
: "${P3_CPR:?falta P3_CPR (ruta de p2arch0_eng.cpr)}"
tmp=${P3_TMP:-/tmp}
functions=$1
shift
days=4
if [[ $1 =~ ^[0-9]+$ ]]; then
	days=$1
	shift
fi
saves=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
	saves+=("$1")
	shift
done
[ "$1" = "--" ] && shift
[ ${#saves[@]} -eq 0 ] && saves=(1 2 3 4 5 6 7 tut0 tut1 tut2 tut3 tut4)
status=0
for s in "${saves[@]}"; do
	[ -f "$tmp/$s.raw" ] || build/tools/p3save/p3save "$P3_CPR" "missav/$s.pat" raw "$tmp/$s.raw" >/dev/null
	probe="$tmp/$s-${functions//,/-}-$days.probe"
	python3 tools/oracle/town_oracle.py "$P3_EXE" "$tmp/$s.raw" "$probe" "$days" "$functions" >/dev/null
	echo "== $s"
	build/tools/oracle/tick_probe "$probe" "$@" || status=1
done
exit $status
