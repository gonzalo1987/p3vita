#!/usr/bin/env bash
# Instala VitaSDK en $VITASDK (por defecto ~/vitasdk, sin sudo) junto con las
# bibliotecas de Vita que usa el proyecto, y las herramientas de compilación
# del host que falten (cmake y ninja con pip --user).
set -euo pipefail

source "$(dirname "$0")/env.sh"

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
	python3 -m pip install --user --upgrade cmake ninja
fi

if [[ ! -x "$VITASDK/bin/vdpm" ]]; then
	tmp=$(mktemp -d)
	trap 'rm -rf -- "$tmp"' EXIT
	git clone --depth 1 https://github.com/vitasdk/vdpm "$tmp/vdpm"
	"$tmp/vdpm/bootstrap-vitasdk.sh" --install-dir "$VITASDK"
fi

# The answer to vdpm's questions, for sessions without a terminal.
vdpm install sdl2 <<< $'y\ny\ny'

arm-vita-eabi-gcc --version | head -n1
cmake --version | head -n1
ninja --version
