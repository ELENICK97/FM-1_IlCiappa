#!/bin/sh
# IL CIAPPA's tests: the host simulator plays every mode, checks the state and records the sound.
set -e
cd "$(dirname "$0")/.."
python3 base/tools/build.py app --host
out=app/build/host
"$out/ilciappa_host" app/tests/basic.txt "$out" | tee "$out/basic.log"
! grep -q "FAIL" "$out/basic.log"
