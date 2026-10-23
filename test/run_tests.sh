#!/usr/bin/env bash
# DrLogueXXL's offline tests (x86, ASan + UBSan): the mpc-vst-plugins host test, then the whole-engine test.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/gen_params.py
third_party/mpc-vst-plugins/tools/test_port.sh vst.json
SRCS=$(python3 -c "import json; print(' '.join(s for s in json.load(open('vst.json'))['build']['sources'] if 'vst_glue' not in s))")
CF=$(python3 -c "import json; print(' '.join(json.load(open('vst.json'))['build']['cflags']))")
mkdir -p build/test
g++ -O1 -g -fsanitize=address,undefined -std=gnu++11 -w $CF $SRCS test/engine_test.cc -lpthread -o build/test/engine_test
build/test/engine_test
