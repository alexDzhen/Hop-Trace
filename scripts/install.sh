#!/usr/bin/env sh
# Builds hoptrace if needed, installs it and lets it run without sudo.
# Usage: scripts/install.sh [prefix]     (default prefix: /usr/local)
set -eu

cd "$(dirname "$0")/.."
PREFIX="${1:-/usr/local}"

if [ ! -x build/hoptrace ]; then
    ./scripts/build.sh
fi

sudo cmake --install build --prefix "$PREFIX"

if sudo setcap cap_net_raw+ep "$PREFIX/bin/hoptrace"; then
    echo "Installed $PREFIX/bin/hoptrace with the CAP_NET_RAW capability."
else
    echo "Installed $PREFIX/bin/hoptrace, but the capability could not be set."
    echo "Install setcap (package libcap2-bin or libcap) and rerun, or run hoptrace with sudo."
fi
