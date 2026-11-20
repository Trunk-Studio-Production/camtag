#!/usr/bin/env bash
# Prepara una sessione cloud di Claude Code: sottomodulo, PlatformIO isolato,
# riferimento CrossPoint e pacchetti del firmware. Si può rilanciare: salta
# quello che c'è già. Lo chiama l'hook SessionStart in .claude/settings.json,
# solo nel cloud (CLAUDE_CODE_REMOTE=true); in locale lo si lancia a mano.
set -euo pipefail
cd "$(dirname "$0")/.."

log() { echo "[cloud-setup] $*" >&2; }

git submodule update --init

if [ ! -x .venv/bin/pio ]; then
  log "installo PlatformIO in .venv"
  # La piattaforma pioarduino vuole Python 3.10 o più nuovo.
  py=""
  for c in python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$c" >/dev/null && "$c" -c 'import sys; sys.exit(sys.version_info < (3, 10))'; then
      py="$c"; break
    fi
  done
  [ -n "$py" ] || { log "serve Python 3.10 o più nuovo"; exit 1; }
  rm -rf .venv
  "$py" -m venv .venv
  .venv/bin/pip install --quiet --disable-pip-version-check platformio
fi

if [ ! -d reference/crosspoint ]; then
  log "clono il riferimento CrossPoint"
  git clone --quiet --depth 1 https://github.com/crosspoint-reader/crosspoint-reader reference/crosspoint \
    || log "clone di CrossPoint non riuscito (serve solo da consultazione)"
fi

# Piattaforma, toolchain e librerie del firmware: scaricarli qui evita che la
# prima compilazione nella sessione si fermi per minuti.
log "scarico i pacchetti del firmware"
(cd device && ../.venv/bin/pio pkg install -e x3 >/dev/null)

log "pronto"
