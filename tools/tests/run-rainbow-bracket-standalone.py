#!/usr/bin/env python3
"""Compile and exercise the production rainbow bracket state helpers."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-rainbow-bracket-") as temp:
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "rainbow-bracket-test"
        source.write_text(r'''#include "doc/layout/BracketNestingState.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  std::vector<SRainbowBracketMark> correct(6);
  const wchar_t chars[] = { L'(', L'[', L'{', L'}', L']', L')' };
  CBracketMatchResolver good;
  for (std::size_t i = 0; i < correct.size(); ++i) good.Consume(chars[i], &correct[i]);
  good.Finish();
  for (const auto& mark : correct) assert(!mark.unmatched);

  std::vector<SRainbowBracketMark> malformed(4);
  const wchar_t badChars[] = { L'(', L'[', L')', L']' };
  CBracketMatchResolver bad;
  for (std::size_t i = 0; i < malformed.size(); ++i) bad.Consume(badChars[i], &malformed[i]);
  bad.Finish();
  assert(malformed[0].unmatched && !malformed[1].unmatched);
  assert(malformed[2].unmatched && !malformed[3].unmatched);

  std::vector<SRainbowBracketMark> unclosed(2);
  CBracketMatchResolver eof;
  eof.Consume(L'(', &unclosed[0]);
  eof.Consume(L'{', &unclosed[1]);
  eof.Finish();
  assert(unclosed[0].unmatched && unclosed[1].unmatched);
  std::cout << "rainbow bracket nesting, mismatch and EOF cases passed\n";
}
''', encoding="utf-8")
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-I", str(ROOT / "sakura_core"),
            str(source), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
