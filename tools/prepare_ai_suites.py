#!/usr/bin/env python3
"""Route the SDK suite loader through Slippy's version adapters, without editing the SDK."""
import pathlib
import sys


def prepare(source: str) -> str:
    for operation in ("AcquireSuite", "ReleaseSuite"):
        original = f"sSPBasic->{operation}("
        if source.count(original) != 1:
            raise ValueError(f"Expected one {original} call; review the updated SDK loader")
        source = source.replace(original, f"Slippy{operation}(")
    return '#include "SuiteCompat.h"\n' + source


if __name__ == "__main__":
    source, target = map(pathlib.Path, sys.argv[1:])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(prepare(source.read_text()))
