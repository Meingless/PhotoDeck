#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$photodeck_root/bin/photodeck" ]; then
    printf '%s\n' 'PhotoDeck henüz derlenmemiş. Önce ./scripts/build.sh çalıştırın.' >&2
    exit 1
fi
exec "$photodeck_root/bin/photodeck" "$@"
