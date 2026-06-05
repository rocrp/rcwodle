#!/usr/bin/env bash
# crosspoint-on-wodle verification: target firmware build + host test suite.
#   ./run_checks.sh            # both
#   ./run_checks.sh target     # scons build only
#   ./run_checks.sh host       # cmake/ctest only
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WHAT="${1:-all}"

if [[ "$WHAT" == "all" || "$WHAT" == "target" ]]; then
    echo "== target build (scons --board=wodle) =="
    # shellcheck disable=SC1090
    source ~/w/_hw/SiFli-SDK/export.sh >/dev/null
    (cd "$HERE/project" && scons --board=wodle -j8 | grep -E "Binary size|error" || true)
    test -f "$HERE/project/build_wodle_hcpu/output/main.bin"
    echo "target: OK ($(stat -f%z "$HERE/project/build_wodle_hcpu/output/main.bin") bytes)"
fi

if [[ "$WHAT" == "all" || "$WHAT" == "host" ]]; then
    echo "== host tests (cmake + ctest) =="
    BUILD_DIR="${CP_TEST_BUILD_DIR:-/tmp/cp_test}"
    cmake -S "$HERE/test" -B "$BUILD_DIR" >/dev/null
    cmake --build "$BUILD_DIR" -j8 >/dev/null
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j8 | grep -E "tests passed|tests failed|Failed"
fi
