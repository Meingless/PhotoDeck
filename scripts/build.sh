#!/usr/bin/env bash
set -euo pipefail
photodeck_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
photodeck_cargo=$(command -v cargo || true)
if [ -z "$photodeck_cargo" ] && [ -x "$HOME/.cargo/bin/cargo" ]; then photodeck_cargo="$HOME/.cargo/bin/cargo"; fi
if [ -z "$photodeck_cargo" ]; then
    printf '%s\n' 'Rust 1.89+ gerekli. https://rustup.rs adresinden kurun.' >&2
    exit 1
fi
cd "$photodeck_root"
"$photodeck_cargo" test --workspace --all-targets --locked --jobs 4
"$photodeck_cargo" build --release --locked --jobs 4
printf '%s\n' 'Rust uygulaması hazır: ./launch.sh'
