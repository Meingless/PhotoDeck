#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")" && pwd)
for photodeck_binary in "$photodeck_root/target/release/photodeck" "$photodeck_root/target/debug/photodeck"; do
    if [ -x "$photodeck_binary" ]; then exec "$photodeck_binary" "$@"; fi
done
printf '%s\n' 'PhotoDeck Rust henüz derlenmemiş. Önce ./scripts/build.sh çalıştırın.' >&2
exit 1
