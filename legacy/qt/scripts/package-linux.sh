#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
photodeck_out="$photodeck_root/dist/PhotoDeck-linux-x86_64"
mkdir -p "$photodeck_out/bin" "$photodeck_out/examples" "$photodeck_out/packaging" "$photodeck_out/scripts"
cp "$photodeck_root/bin/photodeck" "$photodeck_out/bin/"
cp "$photodeck_root/launch.sh" "$photodeck_root/README.md" "$photodeck_root/LICENSE" "$photodeck_out/"
cp "$photodeck_root/examples/Horizon.pdeck" "$photodeck_out/examples/"
cp "$photodeck_root/packaging/photodeck.svg" "$photodeck_out/packaging/"
cp "$photodeck_root/scripts/install-user.sh" "$photodeck_out/scripts/"
tar -C "$photodeck_root/dist" -czf "$photodeck_root/dist/PhotoDeck-linux-x86_64.tar.gz" PhotoDeck-linux-x86_64
printf 'Paket: %s\n' "$photodeck_root/dist/PhotoDeck-linux-x86_64.tar.gz"
