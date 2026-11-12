#!/usr/bin/env python3
"""Copy checked-in Qt compatibility fixtures into the untracked fuzz seed corpus."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent.parent
corpus = root / "fuzz" / "corpus" / "project_decode"
corpus.mkdir(parents=True, exist_ok=True)
for folder, prefix in [(root / "tests/fixtures", "qt-"),
                       (root / "legacy/qt/tests/fixtures", "legacy-")]:
    for source in sorted(folder.glob("*.pdeck")):
        if source.stat().st_size <= 1024 * 1024:
            shutil.copyfile(source, corpus / (prefix + source.name))
source = root / "examples/Horizon.pdeck"
if source.is_file() and source.stat().st_size <= 1024 * 1024:
    shutil.copyfile(source, corpus / "legacy-horizon.pdeck")
print(f"Seed corpus: {len(list(corpus.iterdir()))} files in {corpus}")
