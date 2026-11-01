#!/bin/sh
# IL CIAPPA: the firmware (app/build/ilciappa.fwsc). Needs the JieLi toolchain and the AC79 SDK (see base/README.md);
# on GitHub, .github/workflows/release.yml builds it with the shared FM-1 builder.
#   ./build.sh [--release X.Y]      ./build.sh --host (the simulator)
set -e
cd "$(dirname "$0")"
exec python3 base/tools/build.py app "$@"
