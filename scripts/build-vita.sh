#!/usr/bin/env bash
# Compila el VPK para la Vita y lo deja en dist/.
#
# La compilación va al home de WSL: es más rápido que /mnt/c y evita los
# espacios en la ruta, que vita-pack-vpk no admite.
set -euo pipefail

source "$(dirname "$0")/env.sh"

src=$(cd "$(dirname "$0")/.." && pwd)
build=${P3_VITA_BUILD_DIR:-$HOME/build/p3vita-vita}

cmake -S "$src" -B "$build" \
	-DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
	-DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j "$(nproc)"

mkdir -p "$src/dist"
cp "$build/src/app/p3vita.vpk" "$src/dist/"
echo "VPK: $src/dist/p3vita.vpk"
