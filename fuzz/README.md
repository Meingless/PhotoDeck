# PhotoDeck project parser fuzzing

This separate cargo-fuzz workspace exercises untrusted `.pdeck` bytes in memory.
Accepted inputs must validate and survive a recovery-mode encode/decode round trip,
including active and named selections. The harness never opens input-provided paths.
Production file limits remain unchanged; the harness input cap is 1 MiB.

Install a recent nightly Rust toolchain and `cargo-fuzz`, then run from the repository:

```sh
rustup toolchain install nightly --profile minimal
cargo install cargo-fuzz --locked
python3 fuzz/prepare_corpus.py
cargo +nightly fuzz run project_decode -- -max_total_time=20 -max_len=1048576 -timeout=10 -rss_limit_mb=2048
```

`prepare_corpus.py` copies the existing Qt v1–v7 compatibility fixtures from
`tests/fixtures`, `legacy/qt/tests/fixtures`, and `examples/Horizon.pdeck`; copies,
mutated corpus entries, crash artifacts and instrumented build outputs are ignored.
No second set of binary assets is committed. Keep any discovered minimal reproducer
as an ordinary deterministic regression test before fixing the parser.

For a crash, reproduce with `cargo +nightly fuzz run project_decode PATH_TO_ARTIFACT`.
Longer runs and additional seeds improve coverage; a short successful smoke run does
not prove parser safety or replace the deterministic malformed-file/storage tests.

## Local smoke evidence — 2026-11-08

Linux, cargo-fuzz 0.13.2, nightly Rust 1.102.0 (2026-11-07), default AddressSanitizer:
8 existing fixture seeds; 274 executions in 21 seconds; final libFuzzer counters
`cov: 2999`, `ft: 9494`, corpus 98 entries / 5276 KiB, reported RSS 377 MiB.
The run exited successfully without a crash or sanitizer finding. These counters
are libFuzzer coverage/features, not a percentage of application code coverage.
The local build/run log is `artifacts/fuzz/project-decode-2026-11-08.log` (ignored).
The tooling was installed under `/tmp/photodeck-fuzz-tools` and
`/tmp/photodeck-fuzz-rustup`; the user's default stable toolchain was unchanged.
