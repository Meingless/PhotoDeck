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
    for photodeck_project in "$photodeck_root"/tests/*.pro; do
        photodeck_suite=${photodeck_project##*/}
        photodeck_suite=${photodeck_suite%.pro}
        photodeck_test_build="$photodeck_root/build-$photodeck_suite"
        photodeck_test_binary="photodeck-$photodeck_suite-tests"
        # The original two test projects predate the suite naming convention.
        case "$photodeck_suite" in
            tests) photodeck_test_binary=photodeck-tests ;;
            features)
                photodeck_test_build="$photodeck_root/build-audit"
                photodeck_test_binary=photodeck-feature-audit
                ;;
        esac
        mkdir -p "$photodeck_test_build"
        (cd "$photodeck_test_build" && "$photodeck_qmake" "$photodeck_project" && make -j4 && QT_QPA_PLATFORM=offscreen "./$photodeck_test_binary")
    done
    mkdir -p "$photodeck_root/bin"
    cp "$photodeck_build/bin/photodeck" "$photodeck_root/bin/photodeck"
fi
if command -v strip >/dev/null 2>&1; then strip "$photodeck_root/bin/photodeck"; fi
