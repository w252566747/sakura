#!/usr/bin/env python3
"""Benchmark production literal filtering without Windows UI or document storage."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-realtime-filter-perf-") as temp:
        temp_path = Path(temp)
        (temp_path / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        (temp_path / "extmodule").mkdir()
        (temp_path / "extmodule/CBregexp.h").write_text(r'''#pragma once
#include <cwchar>
#include <string>
using LPCWSTR = const wchar_t*;
constexpr int DLL_SUCCESS = 0;
class CBregexp {
public:
  enum Option { optNothing = 0, optCaseSensitive = 1 };
  int InitDll(const wchar_t*) { return DLL_SUCCESS; }
  bool Compile(const wchar_t*, int) { return false; }
  bool Match(const wchar_t*, int) const { return false; }
  const wchar_t* GetLastMessage() const { return L"unused"; }
};
''', encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "RealtimeTextFilter.h"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

int main() {
  using namespace textfilter;
  Options options;
  options.includeTerms = {L"WARN", L"timeout"};
  options.excludeTerms = {L"DEBUG"};
  Matcher matcher(options);
  std::vector<std::wstring> corpus;
  corpus.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    std::wstring line = L"2026-11-27 INFO request completed";
    if (i % 10 == 0) line = L"2026-11-27 warn HTTP timeout after retry";
    if (i % 20 == 0) line += L" debug trace";
    corpus.push_back(std::move(line));
  }
  for (const std::size_t total : {1000u, 10000u, 50000u, 100000u, 1000000u}) {
    std::size_t matches = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < total; ++i) {
      matches += matcher.Matches(corpus[i % corpus.size()]) ? 1u : 0u;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - begin).count();
    if (matches != total / 20) return 2;
    std::cout << total << " lines: " << elapsed << " ms, matches=" << matches << '\n';
  }
}
''', encoding="utf-8")
        executable = temp_path / "realtime-filter-performance"
        subprocess.run([
            "clang++", "-std=c++17", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
            "-I", str(temp_path), "-I", str(ROOT / "sakura_core/parse"),
            str(ROOT / "sakura_core/parse/RealtimeTextFilter.cpp"), str(temp_path / "main.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
