#!/usr/bin/env python3
"""Measure the production code-fold parser at the requested line counts."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-codefold-performance-") as temp:
        temp_path = Path(temp)
        (temp_path / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        source = temp_path / "main.cpp"
        source.write_text(r'''#include "parse/CodeFoldParser.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main() {
  for (const std::size_t count : {1000u, 10000u, 50000u, 100000u, 1000000u}) {
    std::vector<std::wstring> owned;
    owned.reserve(count);
    for (std::size_t line = 0; line < count; ++line) {
      if (line == 0) owned.emplace_back(L"BEGIN");
      else if (line + 1 == count) owned.emplace_back(L"END;");
      else owned.emplace_back(L"NULL;");
    }
    std::vector<std::wstring_view> lines;
    lines.reserve(count);
    for (const auto& line : owned) lines.emplace_back(line);
    const auto start = std::chrono::steady_clock::now();
    const auto regions = codefold::Parse(lines, codefold::Language::Sql);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
    if (count == 1) assert(regions.empty());
    else assert(regions.size() == 1 && regions[0].firstLine == 0 && regions[0].lastLine == count - 1);
    std::cout << count << " lines: " << elapsed / 1000.0 << " ms, regions=" << regions.size() << '\n';
  }
}
''', encoding="utf-8")
        executable = temp_path / "codefold-performance"
        subprocess.run([
            "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(temp_path), "-I", str(ROOT / "sakura_core"),
            str(ROOT / "sakura_core/parse/CodeFoldParser.cpp"), str(source),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
