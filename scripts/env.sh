# Uso: source scripts/env.sh
# Deja VitaSDK y las herramientas instaladas con pip --user en el PATH.
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$HOME/.local/bin:$PATH"
