#!/bin/sh
# openmedia.library's host tests: makes the samples (make_samples.sh, FFmpeg's command-line
# tool and make_iff.py), checks every file against samples.txt, then the fuzz cases, all
# under AddressSanitizer and UndefinedBehaviorSanitizer.
#   tests/host/run.sh [SAMPLES_DIR] [FUZZ_ROUNDS]
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SAMPLES=${1:-$HERE/samples}
ROUNDS=${2:-300}
BUILD=${BUILD:-$(mktemp -d)}
[ -f "$SAMPLES/wav_pcm16.wav" ] || "$HERE/make_samples.sh" "$SAMPLES"
${CC:-gcc} -std=c99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined -o "$BUILD/test_probe" "$HERE/test_probe.c" "$ROOT/library/om_probe.c"
"$BUILD/test_probe" --check "$HERE/samples.txt" "$SAMPLES"
"$BUILD/test_probe" --fuzz "$SAMPLES" "$ROUNDS"
