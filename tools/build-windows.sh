#!/usr/bin/env bash
set -euo pipefail

# CMake is a native Windows executable. Use native forward-slash paths even
# when the caller disables MSYS argument conversion (for example, Hermes).
source_dir=$(cygpath -m "$MGBA_SOURCE_ROOT")
build_dir=$(cygpath -m "$MGBA_BUILD_ROOT")

# windres otherwise constructs an unquoted absolute preprocessor path,
# which fails when the user's Windows profile contains spaces.
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "-DCMAKE_RC_FLAGS=--preprocessor=gcc --preprocessor-arg=-E --preprocessor-arg=-xc --preprocessor-arg=-DRC_INVOKED" \
    -DBUILD_QT=ON -DBUILD_SDL=ON -DFORCE_QT_VERSION=6 \
    -DBUILD_STATIC=ON -DBUILD_SHARED=OFF
# Limit parallelism to keep first-time builds usable on smaller machines.
jobs=$(nproc)
if (( jobs > 4 )); then jobs=4; fi
cmake --build "$build_dir" --target mgba-qt --parallel "$jobs"
