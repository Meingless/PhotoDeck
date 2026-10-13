#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
photodeck_build=${1:-"$photodeck_root/build"}
mkdir -p "$photodeck_build"
if command -v cmake >/dev/null 2>&1; then
    cmake -S "$photodeck_root" -B "$photodeck_build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$photodeck_build" --parallel 4
    ctest --test-dir "$photodeck_build" --output-on-failure
    mkdir -p "$photodeck_root/bin"
    cp "$photodeck_build/photodeck" "$photodeck_root/bin/photodeck"
else
    photodeck_qmake=$(command -v qmake6 || command -v qmake)
    (cd "$photodeck_build" && "$photodeck_qmake" "$photodeck_root/PhotoDeck.pro" && make -j4)
    mkdir -p "$photodeck_root/build-tests"
    (cd "$photodeck_root/build-tests" && "$photodeck_qmake" "$photodeck_root/tests/tests.pro" && make -j4 && QT_QPA_PLATFORM=offscreen ./photodeck-tests)
    mkdir -p "$photodeck_root/build-audit"
    (cd "$photodeck_root/build-audit" && "$photodeck_qmake" "$photodeck_root/tests/features.pro" && make -j4 && QT_QPA_PLATFORM=offscreen ./photodeck-feature-audit)
    for photodeck_suite in tones selections blending; do
        mkdir -p "$photodeck_root/build-$photodeck_suite"
        (cd "$photodeck_root/build-$photodeck_suite" && "$photodeck_qmake" "$photodeck_root/tests/$photodeck_suite.pro" && make -j4 && QT_QPA_PLATFORM=offscreen "./photodeck-$photodeck_suite-tests")
    done
    mkdir -p "$photodeck_root/bin"
    cp "$photodeck_build/bin/photodeck" "$photodeck_root/bin/photodeck"
fi
if command -v strip >/dev/null 2>&1; then strip "$photodeck_root/bin/photodeck"; fi
