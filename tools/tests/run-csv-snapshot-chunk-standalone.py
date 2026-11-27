#!/usr/bin/env python3
"""Compile and exercise the production bounded CSV snapshot chunk helper."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-csv-snapshot-") as temp:
        source = Path(temp) / "main.cpp"
        source.write_text(r'''#include "parse/CSVTableSnapshotChunk.h"
#include <cassert>
#include <iostream>

int main() {
  using csvtable::snapshot::AppendNextChunk;
  std::wstring snapshot;
  std::size_t offset = 0;
  std::wstring line = L"alpha,";
  line.push_back(static_cast<wchar_t>(0xd83d));
  line.push_back(static_cast<wchar_t>(0xde00));
  line += L",beta";
  while (offset < line.size()) {
    const auto oldOffset = offset;
    assert(AppendNextChunk(snapshot, line.data(), line.size(), offset, 7, 128));
    assert(offset > oldOffset && offset - oldOffset <= 7);
  }
  assert(snapshot == line);

  std::wstring empty;
  offset = 0;
  assert(AppendNextChunk(empty, L"", 0, offset, 3, 0));
  assert(offset == 0 && empty.empty());

  std::wstring bounded = L"1234";
  offset = 0;
  assert(!AppendNextChunk(bounded, L"xy", 2, offset, 1, 5));
  assert(bounded == L"1234" && offset == 0);

  offset = 3;
  assert(!AppendNextChunk(bounded, L"xy", 2, offset, 1, 20));
  assert(bounded == L"1234" && offset == 3);
  std::cout << "production CSV snapshot chunk boundaries passed\n";
}
''', encoding="utf-8")
        executable = Path(temp) / "csv-snapshot-test"
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
            "-I", str(ROOT / "sakura_core"), str(source), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
