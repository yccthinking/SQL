#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
# CMake caches absolute paths. Preserve stale output and start fresh after a move.
if [[ -f build/CMakeCache.txt ]]; then
  cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' build/CMakeCache.txt)
  cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' build/CMakeCache.txt)
  project_dir=$(pwd -P)
  if [[ "$cached_source" != "$project_dir" || "$cached_build" != "$project_dir/build" ]]; then
    if [[ -L build || "$(realpath build)" != "$project_dir/build" ]]; then
      echo "Cannot reset build: directory points outside this project." >&2
      exit 1
    fi
    backup="build.before-move.$(date +%Y%m%d-%H%M%S).$$"
    [[ ! -e "$backup" ]]
    mv -- build "$backup"
    echo "Project path changed. Previous build preserved in: $backup"
  fi
fi
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-14 \
  -DCMAKE_CXX_COMPILER=clang++-14 \
  -DCMAKE_EXE_LINKER_FLAGS=-no-pie
# Keep memory usage bounded in WSL (Debug + AddressSanitizer).
cmake --build build --parallel 4 "$@"
