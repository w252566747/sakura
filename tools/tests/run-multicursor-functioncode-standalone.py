#!/usr/bin/env python3
"""Ensure multi-cursor commands keep unique function IDs."""
from collections import defaultdict
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "sakura_core" / "Funccode_x.hsrc"
MULTI_CURSOR_NAMES = {
    "F_MULTICURSOR_ADD_UP",
    "F_MULTICURSOR_ADD_DOWN",
    "F_MULTICURSOR_REMOVE_LAST",
    "F_MULTICURSOR_CLEAR",
    "F_MULTICURSOR_SELECT_NEXT",
    "F_MULTICURSOR_SELECT_ALL",
}


def main() -> None:
    by_value: dict[int, list[str]] = defaultdict(list)
    found: dict[str, int] = {}
    for line in SOURCE.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"\s*(F_[A-Z0-9_]+)\s*=\s*(\d+)", line)
        if not match:
            continue
        name, raw_value = match.groups()
        value = int(raw_value)
        by_value[value].append(name)
        if name in MULTI_CURSOR_NAMES:
            found[name] = value

    assert set(found) == MULTI_CURSOR_NAMES, "missing multi-cursor function IDs"
    for name, value in found.items():
        assert by_value[value] == [name], f"{name} collides with {by_value[value]} at {value}"
    print("multi-cursor function IDs are unique")


if __name__ == "__main__":
    main()
